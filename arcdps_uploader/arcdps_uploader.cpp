// arcdps_uploader.cpp : Defines the exported functions for the DLL application.
//

#include "arcdps_uploader.h"
#include "imgui.h"
#include <ShlObj.h>
#include <wincred.h>
#include <codecvt>
#include <nlohmann/json.hpp>
#include <fstream>
#include <thread>
#include <chrono>
#include <ctime>
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_NO_ZLIB_APIS
#include "miniz.h"
#include <loguru.hpp>

#define UPLD_WINCRED_NAME L"Arcdps_Uploader_Raidar"

using json = nlohmann::json;

/* dll main -- winapi */
BOOL APIENTRY DllMain(HANDLE hModule, DWORD ulReasonForCall, LPVOID lpReserved) {
	switch (ulReasonForCall) {
	case DLL_PROCESS_ATTACH: dll_init(hModule); break;
	case DLL_PROCESS_DETACH: dll_exit(); break;

	case DLL_THREAD_ATTACH:  break;
	case DLL_THREAD_DETACH:  break;
	}
	return 1;
}

/* dll attach -- from winapi */
void dll_init(HANDLE hModule) {
	return;
}

/* dll detach -- from winapi */
void dll_exit() {
	return;
}

/* export -- arcdps looks for this exported function and calls the address it returns */
extern "C" __declspec(dllexport) void* get_init_addr(char* arcversionstr, void* imguicontext) {
	arcvers = arcversionstr;
	ImGui::SetCurrentContext((ImGuiContext*)imguicontext);
	return mod_init;
}

/* export -- arcdps looks for this exported function and calls the address it returns */
extern "C" __declspec(dllexport) void* get_release_addr() {
	arcvers = 0;
	return mod_release;
}

unsigned char ploop[32] = {};

/* initialize mod -- return table that arcdps will use for callbacks */
arcdps_exports* mod_init() {
	int argc = 1;
	char* argv[] = { "uploader.log", nullptr };
	loguru::init(argc, argv);

	is_open = false;
	ini_enabled = true;
	cred_save = false;

	/*
		Find the GW2 exec path and put our ini in the same folder as the arcdps settings.
	*/
	WCHAR exec_path[MAX_PATH];
	if (GetModuleFileName(0, exec_path, MAX_PATH)) {
		CHAR utf_path[MAX_PATH];
		WideCharToMultiByte(CP_UTF8, 0, exec_path, -1, utf_path, MAX_PATH, NULL, NULL);
		std::string execstr(utf_path);
		fs::path full_exec_path(execstr);
		ini_path = full_exec_path.parent_path() / "addons/arcdps/uploader.ini";
		fs::path log_path = full_exec_path.parent_path() / "addons/arcdps/uploader.log";
		loguru::add_file(log_path.string().c_str(), loguru::Append, loguru::Verbosity_MAX);

		ini.SetUnicode();
		SI_Error error = ini.LoadFile(ini_path.string().c_str());
		if (error < 0) {
			ini_enabled = false;
			LOG_F(INFO, "Failed to load/create INI");
		}
	}
	else {
		ini_enabled = false;
	}

	LOG_F(INFO, "Init");
	/*
		If we successfully loaded or created an ini file, we can load our settings from it.
		Passwords are encrypted using the Windows credential manager.
	*/
	if (ini_enabled) {
		LOG_F(INFO, "INI Enabled");
		cred_save = ini.GetBoolValue("Settings", "SaveUserPass", false);
		if (cred_save) {
			PCREDENTIAL cred{};
			if (CredRead(UPLD_WINCRED_NAME, CRED_TYPE_GENERIC, 0, &cred)) {
				std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
				std::string converted = converter.to_bytes(cred->UserName);
				strcpy_s(username_buf, converted.c_str());

				memcpy_s(pass_buf, 64, cred->CredentialBlob, cred->CredentialBlobSize);
			}
		}
	}

	/* my documents */
	WCHAR my_documents[MAX_PATH];
	HRESULT result = SHGetFolderPath(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, my_documents);

	if (result == S_OK) {
		//TODO: disable the whole thing if we can't find docs?
		CHAR utf_path[MAX_PATH];
		WideCharToMultiByte(CP_UTF8, 0, my_documents, -1, utf_path, MAX_PATH, NULL, NULL);
		std::string mydocs = std::string(utf_path);
		fs::path mydocs_path = fs::path(mydocs);
		log_path = mydocs_path / "Guild Wars 2/addons/arcdps/arcdps.cbtlogs/";

		start_async_refresh_log_list();

		// Create a thread that spins, waiting for uploads to process
		upload_thread_run = true;
		upload_thread = std::thread(upload_thread_loop);
	}

	/* for arcdps */
	arc_exports.size = sizeof(arcdps_exports);
	arc_exports.out_name = "uploader";
	arc_exports.out_build = "0.9.0";
	arc_exports.sig = 0x92485179;
	arc_exports.wnd = mod_wnd;
	arc_exports.combat = mod_combat;
	arc_exports.imgui = mod_imgui;
	return &arc_exports;
}

/* release mod -- return ignored */
uintptr_t mod_release() {
	// Save our settings if we previously loaded/created an ini file
	// Encrypt saved username/passwords using the Windows Credential Manager
	if (ini_enabled) {
		if (cred_save) {
			CREDENTIAL cred{};
			cred.Type = CRED_TYPE_GENERIC;
			cred.TargetName = UPLD_WINCRED_NAME;
			cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
			std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
			std::wstring wide = converter.from_bytes(username_buf);
			const wchar_t* s = wide.c_str();
			LPWSTR copy = (LPWSTR) malloc(128);
			if (copy) { //If we can't alloc 128 bytes, we're screwed but try to continue anyway
				lstrcpy(copy, s);
				cred.UserName = copy;
				cred.CredentialBlob = (LPBYTE) pass_buf;
				cred.CredentialBlobSize = 64;

				bool success = CredWrite(&cred, 0);
				ini.SetBoolValue("Settings", "SaveUserPass", success);
			}
		}
		ini.SaveFile(ini_path.string().c_str());
	}

	// Stop the thread from looping and wait for it to finish executing
	// Otherwise, GW2 will not exit
	upload_thread_run = false;
	ut_cv.notify_all();
	upload_thread.join();

	return uintptr_t();
}

/* window callback -- return is assigned to umsg (return zero to not be processed by arcdps or game) */
uintptr_t mod_wnd(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {

	//Use windows messaging to detect our window toggle, due to ArcDPS using ImGui's keyboard handling for itself?
	static bool KeyAlt = false;
	static bool KeyShift = false;
	static bool KeyU = false;

	switch (uMsg) {
	case WM_SYSKEYDOWN:
	case WM_KEYDOWN:
	{
		if (wParam == VK_MENU) KeyAlt= true;
		else if (wParam == VK_SHIFT) KeyShift = true;
		else if (wParam == 85) KeyU = true;
		break;
	}
	case WM_SYSKEYUP:
	case WM_KEYUP:
	{
		if (wParam == VK_MENU) KeyAlt= false;
		else if (wParam == VK_SHIFT) KeyShift = false;
		else if (wParam == 85) KeyU = false;
		break;
	}
	}

	if (KeyAlt && KeyShift && KeyU) {
		is_open = !is_open;
	}

	return uMsg;
}

/* combat callback -- may be called asynchronously. return ignored */
/* one participant will be party/squad, or minion of. no spawn statechange events. despawn statechange only on marked boss npcs */
uintptr_t mod_combat(cbtevent* ev, ag* src, ag* dst, char* skillname)
{
	return uintptr_t();
}

uintptr_t mod_imgui() {
	if (is_open) {
		poll_async_refresh_log_list();
		poll_async_authentication();

		ImGui::PushStyleVar(ImGuiStyleVar_ChildWindowRounding, 5.0f);

		if (!ImGui::Begin("Uploader", &is_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
			ImGui::End();
			return uintptr_t();
		}

		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.f, 1.f, 0.f, 0.25f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.f, 1.f, 0.f, 0.5f));
		ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.f, 1.f, 0.f, 0.25f));

		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.f, 1.f, 0.f, 0.5f));
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.f, 1.f, 0.f, 0.25f));

		ImGui::Spacing();
		ImGui::Spacing();

		static ImVec2 log_size(450, 258);
		ImGui::TextUnformatted("Recent Logs");
		ImGui::BeginChild("List", log_size, true, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar);

		static bool success_only = false;
		ImGui::Columns(3, "mycolumns");
		float last_col = log_size.x - ImGui::CalcTextSize("View").x * 1.7f;
		ImGui::SetColumnOffset(0, 0);
		ImGui::SetColumnOffset(1, last_col - ImGui::CalcTextSize("00:00PM (Mon Jan 00)").x * 1.15f);
		ImGui::SetColumnOffset(2, last_col);
		ImGui::TextUnformatted("Name"); ImGui::NextColumn();
		ImGui::TextUnformatted("Created"); ImGui::NextColumn();
		ImGui::TextUnformatted(""); ImGui::NextColumn();
		ImGui::Separator();
		static bool selected[30] { false };
		for (int i = 0; i < file_list.size(); ++i) {
			if (cached_logs.count(file_list[i])) {
				const Log& s = cached_logs.at(file_list[i]);
				std::string display;
				if (s.parsed.valid) {
					display = s.parsed.encounter_name;
				}
				else {
					display = s.filename;
				}

				ImVec4 col = ImVec4(1.f, 0.f, 0.f, 1.f);
				if (s.parsed.reward_at > 0.f) {
					col = ImVec4(0.f, 1.f, 0.f, 1.f);
				}
				else if (success_only) {
					continue;
				}

				ImGui::PushStyleColor(ImGuiCol_Text, col);
				ImGui::PushID(s.human_time.c_str());
				ImGui::Selectable(display.c_str(), &selected[i], ImGuiSelectableFlags_SpanAllColumns);
				ImGui::PopID();
				ImGui::PopStyleColor();
				ImGui::NextColumn();
				ImGui::Text(s.human_time.c_str());
				ImGui::NextColumn();
				ImGui::SmallButton("View");
				if (ImGui::IsItemHovered() && s.parsed.valid) {
					ImGui::BeginTooltip();
					create_log_table(s);
					ImGui::EndTooltip();
				}
				ImGui::NextColumn();

				if (file_list.size() < 9 && i == file_list.size() - 1) {
					log_size.y = ImGui::GetCursorPosY();
				} else if (i == 9) {
					log_size.y = ImGui::GetCursorPosY();
				}
			}
		}
		ImGui::Columns(1);
		ImGui::EndChild();

		if (ImGui::Button("Add to Queue")) {
			for (int i = 0; i < file_list.size(); ++i) {
				if (selected[i]) {
					if (cached_logs.count(file_list[i])) {
						const Log& log = cached_logs.at(file_list[i]);
						//Prevent double queueing the same log, inefficient search
						auto result = std::find(pending_upload_queue.begin(), pending_upload_queue.end(), log);
						if (result == pending_upload_queue.end()) {
							pending_upload_queue.push_back(log);
						}
					}
				}
			}

			//Clear selected
			memset(selected, 0, sizeof selected);
		}
		ImGui::SameLine();
		if (ImGui::Button("Refresh")) {
			start_async_refresh_log_list();
		}
		ImGui::SameLine();
		ImGui::Checkbox("Filter Wipes", &success_only);

		if (ImGui::Button("Queue Recent Clears")) {
			std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
			std::chrono::system_clock::time_point four_hours_ago = now - std::chrono::hours(4);
			for (int i = 0; i < file_list.size(); ++i) {
				if (cached_logs.count(file_list[i])) {
					const Log& s = cached_logs.at(file_list[i]);
					if (s.parsed.reward_at > 0.f) {
						if (s.time > four_hours_ago) {
							//Prevent double queueing the same log, inefficient search
							auto result = std::find(pending_upload_queue.begin(), pending_upload_queue.end(), s);
							if (result == pending_upload_queue.end()) {
								pending_upload_queue.push_back(s);
							}
						}
					}
				}
			}
		}

		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::TextUnformatted("Queue");
		ImGui::BeginChild("Upload Queue", ImVec2(450, 100), true);

		for (auto& l : pending_upload_queue) {
			std::string display;
			if ((uint16_t)l.parsed.area_id) {
				display = l.parsed.encounter_name;
			}
			else {
				display = l.filename;
			}
			ImGui::Text("%s - %s", display.c_str(), l.human_time.c_str());
		}

		ImGui::EndChild();

		if (ImGui::Button("Clear Queue")) {
			pending_upload_queue.clear();
		}

		static int item = 0;
		//TODO: Remove these since dps.report doesn't support cats or tags.
		static const char *items[] = { "None", "Guild / Static", "Training", "PUG", "Low Man / Sells" };
		static char tags[64];

		bool upload_started = false;
		if (ImGui::Button("Upload to dps.report")) {
				add_pending_upload_logs(pending_upload_queue, Destination::DPSREPORT, item, tags);
				pending_upload_queue.clear();
		}
		else {
			upload_started = true;
		}

		if (upload_started) {
			if (!upload_queue.empty()) {
				ut_cv.notify_one();
			}
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextUnformatted("Status");
		ImGui::BeginChild("Status Messages", ImVec2(450, 150), true);

		for (const auto& status : status_messages) {
			ImGui::Text(status.msg.c_str());
			if (status.url.size() != 0) {
				if (status.url.size() > 8) {
					ImGui::Text("%s", status.url.substr(8,  status.url.size()).c_str());
				}
				else {
					ImGui::Text("%s", status.url.c_str());
				}
				ImGui::SameLine();
				ImGui::PushID(std::string("Url" + status.url).c_str());
				if (ImGui::SmallButton("Copy")) {
					ImGui::SetClipboardText(status.url.c_str());
				}
				ImGui::PopID();
			}
		}
		static uint8_t status_message_count = 0;
		if (status_messages.size() > status_message_count)
		{
			ImGui::SetScrollPosHere();
		}
		status_message_count = (uint8_t) status_messages.size();

		ImGui::EndChild();

		if (ImGui::Button("Copy & Format Recent")) {
			std::time_t now = std::time(nullptr);
			std::tm* local = std::localtime(&now);
			char buf[64];
			strftime(buf, 64, "__**%b %d %Y**__\n\n", local);

			std::string msg(buf);

			for (const auto& status : status_messages) {
				if (status.url.size() != 0) {
					uint32_t minutes = (uint32_t) status.duration / 60;
					uint32_t secs = status.duration % 60;
					char t[64];
					sprintf_s(t, "%02d:%02d", minutes, secs);
					std::string time(t);
					msg += status.encounter + " - " + time + "\n*" + status.url + "*\n\n";
				}
			}
			ImGui::SetClipboardText(msg.c_str());
		}

		ImGui::PopStyleColor();
		ImGui::PopStyleColor();
		ImGui::PopStyleColor();
		ImGui::PopStyleColor();
		ImGui::PopStyleColor();

		ImGui::End();

		ImGui::PopStyleVar();

		// Pick up any messages from our upload thread
		{
			std::lock_guard<std::mutex> lk(ts_msg_mutex);
			status_messages.insert(status_messages.end(), thread_status_messages.begin(), thread_status_messages.end());
			thread_status_messages.clear();
		}
	}
	return uintptr_t();
}

void create_log_table(const Log& l) {
	auto seconds_to_string = [](uint64_t seconds) -> std::string {
		uint32_t minutes = (uint32_t)seconds / 60;
		float secondsf = fmodf((float)seconds, 60.f);

		char buf[32];
		sprintf(buf, "%02u:%02.0f", minutes, secondsf);
		return std::string(buf);
	};

	static std::map<std::string, ImVec4> colors = {
		{"Druid", ImVec4(17.f/255.f, 122.f/255.f, 101.f/255.f, 1.f)},
		{"Daredevil", ImVec4(133.f/255.f, 146.f/255.f, 158.f/255.f, 1.f)},
		{"Berserker", ImVec4(211.f/255.f, 84.f/255.f, 0.f, 1.f)},
		{"Dragonhunter", ImVec4(52.f/255.f, 152.f/255.f, 219.f/255.f, 1.f)},
		{"Reaper", ImVec4(20.f/255.f, 90.f/255.f, 50.f/255.f, 1.f)},
		{"Chronomancer", ImVec4(142.f/255.f, 68.f/255.f, 173.f/255.f, 1.f)},
		{"Scrapper", ImVec4(230.f/255.f, 126.f/255.f, 34.f/255.f, 1.f)},
		{"Tempest", ImVec4(93.f/255.f, 173.f/255.f, 226.f/255.f, 1.f)},
		{"Herald", ImVec4(84.f/255.f, 153.f/255.f, 199.f/255.f, 1.f)},
		{"Soulbeast", ImVec4(39.f/255.f, 174.f/255.f, 96.f/255.f, 1.f)},
		{"Weaver", ImVec4(192.f/255.f, 57.f/255.f, 43.f/255.f, 1.f)},
		{"Holosmith", ImVec4(243.f/255.f, 156.f/255.f, 18.f/255.f, 1.f)},
		{"Deadye", ImVec4(203.f/255.f, 67.f/255.f, 53.f/255.f, 1.f)},
		{"Mirage", ImVec4(155.f/255.f, 89.f/255.f, 182.f, 1.f)},
		{"Scourge", ImVec4(241.f/255.f, 196.f/255.f, 15.f/255.f, 1.f)},
		{"Spellbreaker", ImVec4(212.f/255.f, 172.f/255.f, 13.f/255.f, 1.f)},
		{"Firebrand", ImVec4(93.f/255.f, 173.f/255.f, 226.f/255.f, 1.f)},
		{"Renegade", ImVec4(148.f/255.f, 49.f/255.f, 38.f/255.f, 1.f)}
	};

	static ImVec2 size = ImVec2(800, 250);
	ImGui::Text("%s (%s)", l.parsed.encounter_name.c_str(), seconds_to_string(l.parsed.encounter_duration).c_str());
	ImGui::Separator();
	ImGui::Spacing();
	ImGui::BeginChild("DPS Table", size, false, ImGuiWindowFlags_NoScrollbar);

	ImGui::Columns(10);
	ImGui::SetColumnOffset(0, 0); //Sub
	ImGui::SetColumnOffset(1, 15); //Class
	ImGui::SetColumnOffset(2, 15 + 55); //Name
	ImGui::SetColumnOffset(3, 15 + 55 + 180); //Account
	ImGui::SetColumnOffset(4, 15 + 55 + 180 + 180); //Boss DPS
	ImGui::SetColumnOffset(5, 15 + 55 + 180 + 180 + 85); //DPS
	ImGui::SetColumnOffset(6, 15 + 55 + 180 + 180 + 85 + 70); //Might
	ImGui::SetColumnOffset(7, 15 + 55 + 180 + 180 + 85 + 70 + 55); //Fury
	ImGui::SetColumnOffset(8, 15 + 55 + 180 + 180 + 85 + 70 + 55 + 55); //Quickness
	ImGui::SetColumnOffset(9, 15 + 55 + 180 + 180 + 85 + 70 + 55 + 55 + 55); //Alacrity
	ImGui::TextUnformatted(""); ImGui::NextColumn();
	ImGui::TextUnformatted("Class"); ImGui::NextColumn();
	ImGui::TextUnformatted("Name"); ImGui::NextColumn();
	ImGui::TextUnformatted("Account"); ImGui::NextColumn();
	ImGui::TextUnformatted("Boss DPS"); ImGui::NextColumn();
	ImGui::TextUnformatted("DPS"); ImGui::NextColumn();
	ImGui::TextUnformatted("Might"); ImGui::NextColumn();
	ImGui::TextUnformatted("Fury"); ImGui::NextColumn();
	ImGui::TextUnformatted("Quick"); ImGui::NextColumn();
	ImGui::TextUnformatted("Alac"); ImGui::NextColumn();
	ImGui::Separator();

	int16_t sub = -1;
	for (const auto& p : l.parsed.players) {
		if (p.subgroup != sub) {
			ImGui::Separator();
			sub = p.subgroup;
		}
		ImGui::Text("%u", p.subgroup); ImGui::NextColumn();
		if (colors.count(p.elite_spec_name)) {
			ImGui::TextColored(colors.at(p.elite_spec_name), "%s", p.elite_spec_name == "Unknown" ? p.profession_name_short.c_str() : p.elite_spec_name_short.c_str());
		} else {
			ImGui::Text("%s", p.elite_spec_name == "Unknown" ? p.profession_name_short.c_str() : p.elite_spec_name_short.c_str()); 
		}
		ImGui::NextColumn();
		ImGui::Text("%s", p.name.c_str()); ImGui::NextColumn();
		ImGui::Text("%s", p.account.c_str()); ImGui::NextColumn();
		ImGui::Text("%.2fk", (float)p.boss_dps / 1000.f); ImGui::NextColumn();
		ImGui::Text("%.2fk", (float)p.dps / 1000.f); ImGui::NextColumn();
		ImGui::Text("%.1f", p.might_avg); ImGui::NextColumn();
		ImGui::Text("%.1f%%", p.fury_avg * 100.f); ImGui::NextColumn();
		ImGui::Text("%.1f%%", p.quickness_avg * 100.f); ImGui::NextColumn();
		ImGui::Text("%.1f%%", p.alacrity_avg * 100.f); ImGui::NextColumn();
	}

	size.y = ImGui::GetCursorPosY();

	ImGui::EndChild();
}

void start_async_refresh_log_list() {
	//Early out if we are already waiting on a refresh
	if (ft_file_list.valid()) return;

	ft_file_list = std::async(std::launch::async, [](fs::path path) {
		std::vector<std::string> file_list;

		for (auto& p : fs::recursive_directory_iterator(path)) {
			if (fs::is_regular_file(p.status())) {
				std::string path = p.path().string();
				if (cached_logs.count(path) == 0) {
					LOG_F(INFO, "Found new log: %s", path.c_str());
					Log log;
					log.path = p.path();
					log.filename = log.path.filename().replace_extension().replace_extension().string();
					using namespace std::chrono;
					log.time = time_point_cast<system_clock::duration>(
						fs::last_write_time(p) - fs::file_time_type::clock::now() + system_clock::now());
					log.parsed.valid = false;
					cached_logs.emplace(path, log);
				}

				file_list.push_back(path);
			}
		}

		LOG_F(INFO, "Start sorting logs");
		std::sort(file_list.begin(), file_list.end(), [&](const std::string& a, const std::string& b) -> bool {
			if (cached_logs.count(a) && cached_logs.count(b)) {
				const Log&log_a = cached_logs.at(a);
				const Log&log_b = cached_logs.at(b);

				return log_a > log_b;
			}
			return false;
		});
		file_list.resize(50);
		LOG_F(INFO, "Finished sorting and truncating logs");

		LOG_F(INFO, "Start parsing logs");
		for (auto& path : file_list) {
			if (cached_logs.count(path)) {
				Log& log = cached_logs.at(path);
				if (!log.parsed.valid) {
					std::time_t tt = std::chrono::system_clock::to_time_t(log.time);
					std::tm* tm = std::localtime(&tt);
					char timestr[64];
					std::strftime(timestr, sizeof timestr, "%I:%M%p (%a %b %d)", tm);
					log.human_time = std::string(timestr);

					log.category = 0;

					LOG_F(INFO, "Parsing: %s", log.filename.c_str());
					parse_async_log(log);
				}
			}
		}
		LOG_F(INFO, "Finished parsing logs");

		return file_list;
	}, log_path);
}

void parse_async_log(Log& aLog) {
	size_t log_file_size;
	FILE *log = fopen(aLog.path.string().c_str(), "rb");
	if (!log) {
		return;
	}

	if (fseek(log, 0, SEEK_END)) {
		return;
	}
	log_file_size = ftell(log);
	rewind(log);

	unsigned char * log_data = nullptr;
	//Attempt to extract. We assume that the zip extension is accurate and the archive contains an evtc file as its only entry
	if (aLog.path.extension().string() == ".zip" || aLog.path.extension().string() == ".zevtc") {
		mz_bool status;
		mz_zip_archive zip_archive;
		mz_zip_zero_struct(&zip_archive);

		status = mz_zip_reader_init_cfile(&zip_archive, log, log_file_size, 0);
		if (!status) {
			const char *error = mz_zip_get_error_string(mz_zip_get_last_error(&zip_archive));
			return;
		}

		log_data = (unsigned char *)mz_zip_reader_extract_to_heap(&zip_archive, 0, &log_file_size, 0);
		if (!log_data) {
			const char *error = mz_zip_get_error_string(mz_zip_get_last_error(&zip_archive));
			return;
		}
	}
	else { // Not a zip file, read directly to memory
		log_data = (unsigned char *)malloc(log_file_size);
		size_t ret = fread(log_data, sizeof(*log_data), log_file_size, log);
		if (ret != log_file_size) {
			return;
		}
		else {
			if (feof(log))
				return;
			else if (ferror(log))
				return;
		}
	}
	fclose(log);

	if (log_data && log_file_size) {
		Revtc::Parser *parser = new Revtc::Parser(log_data, log_file_size);
		Revtc::Log log = parser->parse();
		aLog.parsed = log;
		delete parser;
	}
	free(log_data);
}

void poll_async_refresh_log_list() {
	if (ft_file_list.valid()) {
		if (ft_file_list.wait_for(std::chrono::milliseconds(1)) == std::future_status::ready) {
			file_list = ft_file_list.get();
		}
	}
}

void start_async_authentication() {
	//Early out if we're already trying to authenticate
	if (ft_auth_response.valid()) return;

	ft_auth_response = cpr::PostAsync(cpr::Url{"https://www.gw2raidar.com/api/v2/token"}, cpr::Payload{{"username", std::string(username_buf)}, {"password", std::string(pass_buf)}});

	StatusMessage status;
	status.msg = "Logging into gw2raidar.com...";
	status_messages.push_back(status);
}

void poll_async_authentication() {
	if (ft_auth_response.valid()) {
		if (ft_auth_response.wait_for(std::chrono::milliseconds(1)) == std::future_status::ready) {
			auth_response = ft_auth_response.get();
		}
		else {
			authentication_in_progress = true;
		}
	}

	if (!is_authenticated && auth_response.status_code == 200) {
		is_authenticated = true;
		json parsed = json::parse(auth_response.text);

		auth_token = parsed.value("token", "default");

		StatusMessage status;
		status.msg = "Successfully logged in.";
		status_messages.push_back(status);
	}
	else if (auth_response.status_code == 400) {
		StatusMessage status;
		status.msg = "Log in failed. Check your username/password.";
		status_messages.push_back(status);
	}
}

void add_pending_upload_logs(std::vector<Log>& queue, Destination dest, int category, std::string tags) {
	{
		std::lock_guard<std::mutex> lk(ut_mutex);
		std::reverse(queue.begin(), queue.end());
		for (Log& log : queue) {
			log.dest = dest;
			log.category = category;
			log.tags = tags;
			log.auth_token = auth_token;
			upload_queue.push(log);
		}
	}
	ut_cv.notify_one();
}

void upload_thread_loop() {
	while (upload_thread_run) {
		std::unique_lock<std::mutex> lk(ut_mutex);
		ut_cv.wait(lk);

		bool process_log = false;
		Log log;
		if (!upload_queue.empty()) {
			log = upload_queue.front();
			upload_queue.pop();
			process_log = true;
		}

		lk.unlock();

		if (process_log) {
			std::string display;
			if ((uint16_t)log.parsed.area_id) {
				display = log.parsed.encounter_name;
			}
			else {
				display = log.filename;
			}

			StatusMessage start;
			start.msg = "Uploading " + display + " - " + log.human_time + ".";
			{
				std::lock_guard<std::mutex> lk(ts_msg_mutex);
				thread_status_messages.push_back(start);
			}

			cpr::Response response;
			if (log.dest == Destination::DPSREPORT) {
				response = cpr::Post(
					cpr::Url{"https://dps.report/uploadContent"},
					cpr::Multipart{ {"file", cpr::File{log.path.string()}}, {"json", "1"} }
				);
			}

			StatusMessage status;
			if (response.status_code == 200) {
				json parsed = json::parse(response.text);
				status.msg = "Uploaded " + display + " - " + log.human_time + ".";
				if (log.dest == Destination::DPSREPORT) {
					status.url = parsed.value("permalink", "");
					if ((uint16_t)log.parsed.area_id) {
						status.encounter = log.parsed.encounter_name;
						status.duration = log.parsed.encounter_duration;
					}
					else {
						status.encounter = log.filename;
						status.duration = log.parsed.encounter_duration;
					}
				}
			}
			else if (response.status_code == 401) {
				status.msg = "Upload failed. Invalid Username/Password. Please login again.";
			}
			else if (response.status_code == 400) {
				status.msg = "Upload failed. Invalid File/File Error or Connection Error.";
			}
			else {
				status.msg = "Unknown response.\n" + response.text;
			}

			{
				std::lock_guard<std::mutex> lk(ts_msg_mutex);
				thread_status_messages.push_back(status);
			}
		}
	}
}
