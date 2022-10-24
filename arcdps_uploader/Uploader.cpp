#include "Uploader.h"
#include "imgui/imgui.h"
#include <nlohmann/json.hpp>
#include <ShlObj.h>
#include <thread>
#include "loguru.hpp"

using json = nlohmann::json;

inline auto initStorage(const std::string& path)
{
	using namespace sqlite_orm;
	return make_storage(path,
						make_table("logs",
								   make_column("id", &Log::id, autoincrement(), primary_key()),
								   make_column("path", &Log::path),
								   make_column("filename", &Log::filename),
								   make_column("human_time", &Log::human_time),
								   make_column("time", &Log::time),
								   make_column("uploaded", &Log::uploaded),
								   make_column("error", &Log::error),
								   make_column("report_id", &Log::report_id),
								   make_column("permalink", &Log::permalink),
								   make_column("boss_id", &Log::boss_id),
								   make_column("boss_name", &Log::boss_name),
								   make_column("players_json", &Log::players_json),
								   make_column("json_available", &Log::json_available),
								   make_column("success", &Log::success)
						),
						make_table("webhooks",
								   make_column("id", &Webhook::id, autoincrement(), primary_key()),
								   make_column("name", &Webhook::name),
								   make_column("url", &Webhook::url),
								   make_column("raids", &Webhook::raids),
								   make_column("fractals", &Webhook::fractals),
								   make_column("strikes", &Webhook::strikes),
								   make_column("golems", &Webhook::golems),
								   make_column("wvw", &Webhook::wvw),
								   make_column("filter", &Webhook::filter),
								   make_column("filter_min", &Webhook::filter_min),
								   make_column("success", &Webhook::success)
						),
						make_table("usertokens",
									make_column("id", &UserToken::id, autoincrement(), primary_key()),
									make_column("value", &UserToken::value),
									make_column("disabled", &UserToken::disabled)
						)
		);
}
using Storage = decltype(initStorage(""));
static std::unique_ptr<Storage> storage;

Uploader::Uploader(fs::path data_path, std::optional<fs::path> custom_log_path)
	: is_open(false)
	, in_combat(false)
	, ini_enabled(true)
{
	//INI
	ini_path = data_path / "uploader.ini";
	ini.SetUnicode();
	SI_Error error = ini.LoadFile(ini_path.string().c_str());
	if (error == SI_OK) {
		LOG_F(INFO, "Loaded INI file");
	}

	//Sqlite Database
	fs::path db_path = data_path / "uploader.db";
	LOG_F(INFO, "DB Path: %s", db_path.string().c_str());
	storage = std::make_unique<Storage>(initStorage(db_path.string()));

	storage->sync_schema(true);
	storage->open_forever();

	userTokens = storage->get_all<UserToken>();
	if (userTokens.size() == 0)
	{
		userToken.id = -1;
		userToken.value = "";
		userToken.disabled = true;
		storage->insert(userToken);
		userTokens = storage->get_all<UserToken>();
	}
	userToken.id = userTokens.front().id;
	userToken.value = userTokens.front().value;
	userToken.disabled = userTokens.front().disabled;
	memset(userToken.value_buf, 0, sizeof(userToken.value_buf));
	if (userToken.disabled) {
		memcpy(userToken.value_buf, "--DISABLED--", sizeof("--DISABLED--"));
	} else {
		memcpy(userToken.value_buf, userToken.value.c_str(), userToken.value.size());
	}

	//Webhooks
	webhooks = storage->get_all<Webhook>();
	for (auto& wh : webhooks)
	{
		memset(wh.name_buf, 0, 64);
		memcpy(wh.name_buf, wh.name.c_str(), wh.name.size());
		memset(wh.url_buf, 0, 192);
		memcpy(wh.url_buf, wh.url.c_str(), wh.url.size());
		memset(wh.filter_buf, 0, 128);
		memcpy(wh.filter_buf, wh.filter.c_str(), wh.filter.size());
	}


	if (custom_log_path) {
		log_path = *custom_log_path;
	} else {
		/* my documents */
		WCHAR my_documents[MAX_PATH];
		HRESULT result = SHGetFolderPath(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, my_documents);
		if (result == S_OK) {
			//TODO: disable the whole thing if we can't find docs?
			CHAR utf_path[MAX_PATH];
			WideCharToMultiByte(CP_UTF8, 0, my_documents, -1, utf_path, MAX_PATH, NULL, NULL);
			std::string mydocs = std::string(utf_path);
			fs::path mydocs_path = fs::path(mydocs);
			LOG_F(INFO, "Documents Path: %s", mydocs_path.string().c_str());
			log_path = mydocs_path / "Guild Wars 2\\addons\\arcdps\\arcdps.cbtlogs\\";
		}
		else {
			LOG_F(ERROR, "Failed to find Documents paths. Fatal.");
		}
	}

	LOG_F(INFO, "Logs Path: %s", log_path.string().c_str());
	if (!std::filesystem::exists(log_path))
	{
		{
			StatusMessage msg;
			msg.msg = "Log path not found. Is Arcdps logging enabled?";
			std::lock_guard<std::mutex> lk(ts_msg_mutex);
			thread_status_messages.push_back(msg);
		}
	}
}

Uploader::~Uploader()
{
	LOG_F(INFO, "Uploader destructor begin...");
	// Save our settings if we previously loaded/created an ini file
	if (ini_enabled) {
		ini.SaveFile(ini_path.string().c_str());
	}

	// Stop the thread from looping and wait for it to finish executing
	// Otherwise, GW2 will not exit
	upload_thread_run = false;
	ut_cv.notify_all();
	upload_thread.join();
}

uintptr_t Uploader::imgui_tick()
{
#ifdef STANDALONE
	if (1) {
#else
	if (is_open) {
#endif
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);

		if (!ImGui::Begin("Uploader", &is_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
			ImGui::End();
			return uintptr_t();
		}

		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.f, 1.f, 0.f, 0.25f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.f, 1.f, 0.f, 0.5f));
		ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.f, 1.f, 0.f, 0.25f));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.f, 1.f, 0.f, 0.5f));
		ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.f, 1.f, 0.f, 0.25f));

		static bool success_only = false;

		static ImVec2 log_size(450, 258);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Recent Logs");
		ImGui::SameLine(450.f - 170.f);
		ImGui::Checkbox("Filter Wipes", &success_only);
		ImGui::SameLine(450.f - 54.f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f, 3.f));
		if (ImGui::Button("Refresh")) {
			start_async_refresh_log_list();
		}
		ImGui::PopStyleVar();

		ImGui::BeginChild("List", log_size, true, ImGuiWindowFlags_NoScrollbar);

		ImGui::Columns(3, "mycolumns");
		float last_col = log_size.x - ImGui::CalcTextSize("View").x * 1.9f;
		ImGui::SetColumnOffset(0, 0);
		ImGui::SetColumnOffset(1, last_col - ImGui::CalcTextSize("00:00PM (Mon Jan 00)").x * 1.1f);
		ImGui::SetColumnOffset(2, last_col);
		ImGui::TextUnformatted("Name"); ImGui::NextColumn();
		ImGui::TextUnformatted("Created"); ImGui::NextColumn();
		ImGui::TextUnformatted(""); ImGui::NextColumn();
		ImGui::Separator();
		static bool selected[75] { false };
		for (int i = 0; i < logs.size(); ++i) {
			Log& s = logs.at(i);
			std::string display;
			if (s.uploaded) {
				display = s.boss_name;
			}
			else {
				display = s.filename;
			}

			ImVec4 col = ImVec4(1.f, 0.f, 0.f, 1.f);
			if (s.success) {
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
			ImGui::SetItemAllowOverlap();
			ImGui::NextColumn();
			ImGui::Text(s.human_time.c_str());
			ImGui::NextColumn();
			if (s.uploaded) {
				ImGui::PushID(s.filename.c_str());
				if (ImGui::SmallButton("View"))
				{
					if (!s.permalink.empty()) {
						int sz = MultiByteToWideChar(CP_UTF8, 0, s.permalink.c_str(), (int)s.permalink.size(), 0, 0);
						std::wstring wstr(sz, 0);
						MultiByteToWideChar(CP_UTF8, 0, s.permalink.c_str(), (int)s.permalink.size(), &wstr[0], sz);
						ShellExecute(0, 0, wstr.c_str(), 0, 0, SW_SHOW);
					}
				}
				ImGui::PopID();
			}
			//if (ImGui::IsItemHovered() && s.uploaded) {
			//	ImGui::BeginTooltip();
			//	create_log_table(s);
			//	ImGui::EndTooltip();
			//}
			ImGui::NextColumn();

			if (logs.size() < 9 && i == logs.size() - 1) {
				log_size.y = ImGui::GetCursorPosY();
			} else if (i == 9) {
				log_size.y = ImGui::GetCursorPosY();
			}
		}
		ImGui::Columns();
		ImGui::EndChild();

		if (ImGui::Button("Copy Selected"))
		{
			std::string msg;
			for (int i = 0; i < logs.size(); ++i) {
				if (selected[i]) {
					const Log& s = logs.at(i);
					msg += s.permalink + "\n";
				}
			}
			ImGui::SetClipboardText(msg.c_str());
		}

		ImGui::SameLine();

		if (ImGui::Button("Copy & Format Recent Clears"))
		{
			std::time_t now = std::time(nullptr);
			std::tm* local = std::localtime(&now);
			char buf[64];
			strftime(buf, 64, "__**%b %d %Y**__\n\n", local);

			std::string msg(buf);

			std::chrono::system_clock::time_point current = std::chrono::system_clock::now();
			std::chrono::system_clock::time_point past = current - std::chrono::minutes(150);
			for (int i = 0; i < logs.size(); ++i) {
				const Log& s = logs.at(i);
				if (s.uploaded && s.success) {
					if (s.time > past) {
						msg += s.boss_name + " - " + "\n*" + s.permalink + "*\n\n";
					}
				}
			}
			ImGui::SetClipboardText(msg.c_str());
		}

		ImGui::Spacing();
		ImGui::Spacing();

		ImGui::Separator();
		ImGui::TextUnformatted("Status");
		ImGui::BeginChild("Status Messages", ImVec2(450, 150), true);

		for (const auto& status : status_messages) {
			ImGui::Text(status.msg.c_str());
			if (status.log_id > 0) {
				auto log = storage->get_pointer<Log>(status.log_id);
				if (log) {
					if (log->permalink.size() > 8) {
						ImGui::Text("%s", log->permalink.substr(8,  log->permalink.size()).c_str());
					}
					else {
						ImGui::Text("%s", log->permalink.c_str());
					}
					ImGui::SameLine();
					ImGui::PushID(std::string("Url" + log->permalink).c_str());
					if (ImGui::SmallButton("Copy")) {
						ImGui::SetClipboardText(log->permalink.c_str());
					}
					ImGui::PopID();
				}
			}
		}
		static uint8_t status_message_count = 0;
		if (status_messages.size() > status_message_count)
		{
			ImGui::SetScrollHereY();
		}
		status_message_count = (uint8_t) status_messages.size();

		ImGui::EndChild();

		if (ImGui::CollapsingHeader("Options"))
		{
			if (ImGui::TreeNode("userToken"))
			{
				ImGui::PushItemWidth(250);
				ImGui::InputText("userToken", userToken.value_buf, sizeof(userToken.value_buf), (userToken.disabled && ImGuiInputTextFlags_ReadOnly) );
				ImGui::PopItemWidth();
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::Text("userToken used by dps.report. Do not share this token with others, unless you know what you are doing!");
					ImGui::EndTooltip();
				}
				if (ImGui::Button("Save") && !userToken.disabled)
				{
					userToken.value = userToken.value_buf;
					storage->update(userToken);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::Text("Applies the userToken. It will then be used for future uploads.");
					ImGui::EndTooltip();
				}
				ImGui::SameLine();
				if (ImGui::Button("Reset"))
				{
					ImGui::OpenPopup("Reset_Confirm");
				}
				if (ImGui::BeginPopup("Reset_Confirm"))
				{
					if (ImGui::Button("Confirm"))
					{
						memset(userToken.value_buf, 0, sizeof(userToken.value_buf));
						userToken.value = userToken.value_buf;
						userToken.disabled = false;
						storage->update(userToken);
					}
					ImGui::EndPopup();
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::Text("Clear the current userToken. On the next upload a new token is generated wich will then be used for future uploads.");
					ImGui::Text("Your current userToken will be lost!");
					ImGui::EndTooltip();
				}
				ImGui::SameLine();
				if (userToken.disabled) {
					if (ImGui::Button("Enable")) {
						memset(userToken.value_buf, 0, sizeof(userToken.value_buf));
						memcpy(userToken.value_buf, userToken.value.c_str(), userToken.value.size());
						userToken.disabled = false;
						storage->update(userToken);
					}
				} else {
					if (ImGui::Button("Disable")) {
						memset(userToken.value_buf, 0, sizeof(userToken.value_buf));
						memcpy(userToken.value_buf, "--DISABLED--", sizeof("--DISABLED--"));
						userToken.disabled = true;
						storage->update(userToken);
					}
				}
				if (ImGui::IsItemHovered()) {
					ImGui::BeginTooltip();
					ImGui::Text("If the userToken is disabled, it will no longer be sent to dps.report. This means that a new userToken will be generated for every upload.");
					ImGui::EndTooltip();
				}
				ImGui::TreePop();
			}
			if (ImGui::TreeNode("Webhooks (Discord)"))
			{
				for (auto& wh : webhooks)
				{
					ImGui::BeginChild(wh.name.c_str(), ImVec2(ImGui::GetContentRegionAvailWidth(), 148), true);

					ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth() - ImGui::CalcTextSize("Filter").x - 1);
					ImGui::InputText("Name", wh.name_buf, 64);
					ImGui::PopItemWidth();
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("For display purposes only (64 characters max)");
						ImGui::EndTooltip();
					}

					ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth() - ImGui::CalcTextSize("Filter").x - 1);
					ImGui::InputText("URL", wh.url_buf, 192);
					ImGui::PopItemWidth();
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("The webhook URL, copy and paste from Discord");
						ImGui::EndTooltip();
					}

					ImGui::Checkbox("Raids", &wh.raids);
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Use this webhook for raid logs");
						ImGui::EndTooltip();
					}
					ImGui::SameLine();

					ImGui::Checkbox("Fractals", &wh.fractals);
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Use this webhook for fractal logs");
						ImGui::EndTooltip();
					}
					ImGui::SameLine();

					ImGui::Checkbox("Strikes", &wh.strikes);
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Use this webhook for strike logs");
						ImGui::EndTooltip();
					}
					ImGui::SameLine();

					ImGui::Checkbox("Golems", &wh.golems);
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Use this webhook for golem logs");
						ImGui::EndTooltip();
					}
					ImGui::SameLine();

					ImGui::Checkbox("WvW", &wh.wvw);
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Use this webhook for WvW logs");
						ImGui::EndTooltip();
					}

					ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth() - ImGui::CalcTextSize("Filter").x - 1);
					ImGui::InputText("Filter", wh.filter_buf, 128);
					ImGui::PopItemWidth();
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Account Names (account.1234, another.5677, ...)");
						ImGui::Text("Logs will only be posted if at least the minimum number of accounts listed here are present");
						ImGui::EndTooltip();
					}

					ImGui::PushItemWidth(ImGui::GetContentRegionAvailWidth() * 0.25f);
					ImGui::InputInt("Min", &wh.filter_min, 1, 2);
					ImGui::PopItemWidth();
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Use this webhook only if the minimum number of accounts\nfrom the list above are present");
						ImGui::EndTooltip();
					}
					ImGui::SameLine();

					ImGui::Checkbox("Clears Only", &wh.success);
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Use this webhook for clears/success only");
						ImGui::EndTooltip();
					}

					if (ImGui::Button("Save"))
					{
						wh.name = wh.name_buf;
						wh.url = wh.url_buf;
						wh.filter = wh.filter_buf;
						storage->update(wh);
					}
					ImGui::SameLine();

					if (ImGui::Button("Delete"))
					{
						ImGui::OpenPopup("Delete_Confirm");
					}
					if (ImGui::BeginPopup("Delete_Confirm"))
					{
						if (ImGui::Button("Confirm"))
						{
							storage->remove<Webhook>(wh.id);
							webhooks = storage->get_all<Webhook>();
							for (auto& wh : webhooks)
							{
								memset(wh.name_buf, 0, 64);
								memcpy(wh.name_buf, wh.name.c_str(), wh.name.size());
								memset(wh.url_buf, 0, 192);
								memcpy(wh.url_buf, wh.url.c_str(), wh.url.size());
								memset(wh.filter_buf, 0, 128);
								memcpy(wh.filter_buf, wh.filter.c_str(), wh.filter.size());
							}
						}
						ImGui::EndPopup();
					}

					ImGui::EndChild();
				}

				ImGui::Separator();
				if (ImGui::Button("Add Webhook"))
				{
					Webhook nwh = {};
					nwh.id = -1;
					nwh.raids = true;
					nwh.fractals = true;
					nwh.strikes = true;
					nwh.golems = true;
					nwh.wvw = true;
					nwh.success = true;
					nwh.filter_min = 10;
					storage->insert(nwh);
					webhooks = storage->get_all<Webhook>();
					for (auto& wh : webhooks)
					{
						memset(wh.name_buf, 0, 64);
						memcpy(wh.name_buf, wh.name.c_str(), wh.name.size());
						memset(wh.url_buf, 0, 192);
						memcpy(wh.url_buf, wh.url.c_str(), wh.url.size());
						memset(wh.filter_buf, 0, 128);
						memcpy(wh.filter_buf, wh.filter.c_str(), wh.filter.size());
					}
				}
				
				ImGui::TreePop();
			}
		}

		if (in_combat) 
		{
			ImGui::TextColored(ImVec4(1.f, 0.f, 0.f, 1.f), "In Combat - Uploads Disabled");
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

	if (!in_combat)
	{
		poll_async_refresh_log_list();
	}

	return uintptr_t();
}

void Uploader::create_log_table(Log& l) {
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


	if (l.players)
	{
		/*
		static ImVec2 size = ImVec2(800, 250);
		ImGui::Text("%s (%s)", l.encounter_name.c_str(), seconds_to_string(l.parsed.encounter_duration).c_str());
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
		*/
	}
}

void Uploader::check_webhooks(int log_id)
{
	auto log = storage->get_pointer<Log>(log_id);
	if (log)
	{
		if (!log->players) {
			try
			{
				json players = json::parse(log->players_json);
				log->players = std::make_optional<json>(players);
			}
			catch (json::parse_error& e)
			{
				LOG_F(ERROR, "Failed to parse player json for %s: %s", log->filename.c_str(), e.what());
			}
		}

		Revtc::BossCategory category = Revtc::Parser::encounterCategory((Revtc::BossID)log->boss_id);
		for (const auto& wh : webhooks)
		{
			bool process = true;
			if (!log->success && wh.success) process = false;
			if (category == Revtc::BossCategory::RAIDS && !wh.raids) process = false;
			if (category == Revtc::BossCategory::FRACTALS && !wh.fractals) process = false;
			if (category == Revtc::BossCategory::STRIKES && !wh.strikes) process = false;
			if (category == Revtc::BossCategory::GOLEMS && !wh.golems) process = false;
			if (category == Revtc::BossCategory::WVW && !wh.wvw) process = false;

			if (wh.filter.size() > 5)
			{
				std::vector<std::string> accounts;
				std::string account;
				std::istringstream accountStream(wh.filter);
				while (std::getline(accountStream, account, ','))
				{
					if (std::isspace(account.front()))
					{
						account = account.substr(1);
					}
					if (account.size() > 0 && std::isspace(account.back()))
					{
						account.pop_back();
					}
					std::transform(account.begin(), account.end(), account.begin(), (int(*)(int)) std::tolower);
					accounts.push_back(account);
				}

				if (log->players && log->players->is_object())
				{
					const auto& players = *log->players;
					int found = 0;
					for (auto it = players.begin(); it != players.end(); ++it)
					{
						auto& display_name = it.value().value("display_name", "");
						std::transform(display_name.begin(), display_name.end(), display_name.begin(), (int(*)(int)) std::tolower);
						if (std::find(accounts.begin(), accounts.end(), display_name) != accounts.end())
						{
							found++;
						}
					}
					int required = (int) min(wh.filter_min, accounts.size());
					LOG_F(INFO, "Webhook (%s) - %s - Found/Required: %d/%d", wh.name.c_str(), log->boss_name.c_str(),found, required);
					if (found < required)
					{
						process = false;
					}
				}
				else
				{
					LOG_F(ERROR, "Players json was not an object.");
				}

				if (process) {
					LOG_F(INFO, "Executing webhook \"%s\" for %s (%s)", wh.name.c_str(), log->filename.c_str(), log->boss_name.c_str());
					auto webhook_future = std::async(std::launch::async, [](const Webhook& wh, Log log) {
							cpr::Response response;
							response = cpr::Post(
								cpr::Url{wh.url},
								cpr::Multipart{ {"content", log.boss_name + " - *" + log.human_time + "*" + "\n" + log.permalink} }
							);
					}, wh, *log);
				}
			}

		}
	}
}

void Uploader::start_async_refresh_log_list() {
	LOG_F(INFO, "Starting Async Log Refresh");
	using namespace sqlite_orm;
	//Early out if we are already waiting on a refresh
	if (ft_file_list.valid()) return;
	if (!std::filesystem::exists(log_path)) return;

	ft_file_list = std::async(std::launch::async, [&](fs::path path) {
		std::vector<Log> file_list;
		auto filenames = storage->select(&Log::filename);
		std::set<std::string> filename_set(filenames.begin(), filenames.end());

		storage->begin_transaction();
		for (const auto& p : fs::recursive_directory_iterator(path)) {
			if (fs::is_regular_file(p.status())) {
				const auto& extension = p.path().extension();
				auto& fn = p.path().filename().replace_extension().replace_extension().string();
				if (extension != ".zevtc" && extension != ".evtc") continue;
				if (filename_set.count(fn) == 0) {
					LOG_F(INFO, "Found new log: %s", p.path().string().c_str());
					Log log;
					log.id = -1;
					log.path = p;
					log.filename = fn;
					//get_time needs separators to parse
					auto temp = log.filename;
					temp.insert(4, "-");
					temp.insert(7, "-");
					temp.insert(13, "-");
					temp.insert(16, "-");
					std::tm tm = {};
					std::stringstream ss(temp);
					ss >> std::get_time(&tm, "%Y-%m-%d-%H-%M-%S");
					if (ss.fail())
					{
						LOG_F(INFO, "Failed to parse time.");
					}
					tm.tm_isdst = -1;
					log.time = std::chrono::system_clock::from_time_t(std::mktime(&tm));

					char timestr[64];
					std::strftime(timestr, sizeof timestr, "%I:%M%p (%a %b %d)", &tm);
					log.human_time = std::string(timestr);

					log.uploaded = false;
					log.error = false;
					log.report_id = "";
					log.permalink = "";
					log.boss_id = 0;
					log.json_available = false;
					log.success = false;

					try
					{
						log.id = storage->insert(log);
					}
					catch (std::system_error e)
					{
						LOG_F(ERROR, "Sqlite insert error: %s", e.what());
					}
					catch (...)
					{
						LOG_F(ERROR, "Unknown Sqlite insert error.");
					}
				}
			}
		}
		storage->commit();

		file_list = storage->get_all<Log>(order_by(&Log::time).desc(), limit(75));

		std::vector<int> queue;
		for (auto& log : file_list)
		{
			if (!log.uploaded) queue.push_back(log.id);
		}
		add_pending_upload_logs(queue);

		return file_list;
	}, log_path);
	refresh_time = std::chrono::system_clock::now();
}

void Uploader::poll_async_refresh_log_list() {
	if (ft_file_list.valid()) {
		if (ft_file_list.wait_for(std::chrono::milliseconds(1)) == std::future_status::ready) {
			logs = ft_file_list.get();
		}
	}

	auto now = std::chrono::system_clock::now();
	auto diff = now - refresh_time;
	if (diff > std::chrono::minutes(1)) {
		start_async_refresh_log_list();
		refresh_time = now;
	}

	// Upload Thread
	if (!upload_queue.empty())
	{
		ut_cv.notify_one();
	}
}

void Uploader::start_upload_thread()
{
	LOG_F(INFO, "Starting Upload Thread");
	// Create a thread that spins, waiting for uploads to process
	upload_thread_run = true;
	upload_thread = std::thread(&Uploader::upload_thread_loop, this);
}

void Uploader::add_pending_upload_logs(std::vector<int>& queue) {
	if (queue.empty()) return;
	{
		std::lock_guard<std::mutex> lk(ut_mutex);
		for (int log_id : queue) {
			if (std::find(upload_queue.begin(), upload_queue.end(), log_id) == upload_queue.end())
			{
				upload_queue.push_back(log_id);
			}
		}
	}
	ut_cv.notify_one();
}

void Uploader::upload_thread_loop() {
	while (upload_thread_run) {
		std::unique_lock<std::mutex> lk(ut_mutex);
		ut_cv.wait(lk);

		bool process_log = false;
		int log_id;
		if (!upload_queue.empty()) {
			log_id = upload_queue.front();
			upload_queue.pop_front();
			process_log = true;
		}

		lk.unlock();

		if (process_log) {
			std::string display;
			auto log = storage->get_pointer<Log>(log_id);
			if (!log) continue;

			display = log->filename;

			StatusMessage start;
			start.msg = "Uploading " + display + " - " + log->human_time + ".";
			{
				std::lock_guard<std::mutex> lk(ts_msg_mutex);
				thread_status_messages.push_back(start);
			}

			cpr::Response response;
			if (userToken.disabled) {
				response = cpr::Post(
					cpr::Url{"https://dps.report/uploadContent"},
					cpr::Multipart{ {"file", cpr::File{log->path.string()}}, {"json", "1"} }
					//cpr::Header{{"accept-encoding", "gzip, deflate"}}
				);
			} else {
				response = cpr::Post(
					cpr::Url{"https://dps.report/uploadContent"},
					cpr::Parameters{{"userToken", userToken.value}},
					cpr::Multipart{ {"file", cpr::File{log->path.string()}}, {"json", "1"} }
					//cpr::Header{{"accept-encoding", "gzip, deflate"}}
				);
			}

			StatusMessage status;
			status.log_id = -1;
			if (response.status_code == 200) {
				json parsed = json::parse(response.text);
				
				log->uploaded = true;
				log->report_id = parsed["id"].get<std::string>();
				log->permalink = parsed["permalink"].get<std::string>();
				json encounter = parsed["encounter"];
				log->boss_id = encounter["bossId"].get<int>();
				log->boss_name = encounter["boss"].get<std::string>();
				log->players_json = parsed["players"].dump();
				log->json_available = encounter["jsonAvailable"].get<bool>();
				log->success = encounter["success"].get<bool>();
				auto token = parsed["userToken"].get<std::string>();

				status.msg = "Uploaded " + display + " - " + log->human_time + ".";
				status.log_id = log->id;

				if (userToken.value.empty() && !userToken.disabled && token.size() <= sizeof(userToken.value_buf)) {
					memset(userToken.value_buf, 0, sizeof(userToken.value_buf));
					memcpy(userToken.value_buf, token.c_str(), token.size());
					userToken.value = userToken.value_buf;
					storage->update(userToken);
				}
				else if (token != userToken.value) {
					status.msg = "ERROR: Configured userToken did not work. Maybe a wrong token was used?";
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
				LOG_F(INFO, "Upload failed: %s - %d, %s, %s", log->filename, response.status_code, response.error.message, response.text);
				log->uploaded = true;
				log->error = true;
			}

			try
			{
				storage->update(*log);
				if (log->uploaded && !log->error) check_webhooks(log->id);
			}
			catch (std::system_error e)
			{
				LOG_F(ERROR, "Failed to update log: %s", e.what());
			}

			{
				std::lock_guard<std::mutex> lk(ts_msg_mutex);
				thread_status_messages.push_back(status);
			}
		}
	}
}
