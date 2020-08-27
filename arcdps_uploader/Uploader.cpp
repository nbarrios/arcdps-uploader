#include "Uploader.h"
#include "imgui.h"
#include <nlohmann/json.hpp>
#include <ShlObj.h>
#include <thread>
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_NO_ZLIB_APIS
#include "miniz.h"
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
								   make_column("json_available", &Log::json_available),
								   make_column("success", &Log::success)
						));
}
using Storage = decltype(initStorage(""));
static std::unique_ptr<Storage> storage;

Uploader::Uploader(fs::path data_path)
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
	storage = std::make_unique<Storage>(initStorage(db_path.string()));
	storage->open_forever();
	storage->sync_schema(true);

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
	}
}

Uploader::~Uploader()
{
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

		static bool success_only = false;

		static ImVec2 log_size(450, 258);
		ImGui::AlignFirstTextHeightToWidgets();
		ImGui::TextUnformatted("Recent Logs");
		ImGui::SameLine(ImGui::GetContentRegionAvailWidth() - 170.f);
		ImGui::Checkbox("Filter Wipes", &success_only);
		ImGui::SameLine(ImGui::GetContentRegionAvailWidth() - 53.f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.f, 3.f));
		if (ImGui::Button("Refresh")) {
			start_async_refresh_log_list();
		}
		ImGui::PopStyleVar();

		ImGui::BeginChild("List", log_size, true, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar);

		ImGui::Columns(3, "mycolumns");
		float last_col = log_size.x - ImGui::CalcTextSize("View").x * 1.7f;
		ImGui::SetColumnOffset(0, 0);
		ImGui::SetColumnOffset(1, last_col - ImGui::CalcTextSize("00:00PM (Mon Jan 00)").x * 1.15f);
		ImGui::SetColumnOffset(2, last_col);
		ImGui::TextUnformatted("Name"); ImGui::NextColumn();
		ImGui::TextUnformatted("Created"); ImGui::NextColumn();
		ImGui::TextUnformatted(""); ImGui::NextColumn();
		ImGui::Separator();
		static bool selected[75] { false };
		for (int i = 0; i < logs.size(); ++i) {
			const Log& s = logs.at(i);
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
			if (ImGui::Selectable(display.c_str(), &selected[i], ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
			{
				if (ImGui::IsMouseDoubleClicked(0))
				{
					ImGui::SetClipboardText(s.permalink.c_str());
				}
			}
			ImGui::PopID();
			ImGui::PopStyleColor();
			ImGui::NextColumn();
			ImGui::Text(s.human_time.c_str());
			ImGui::NextColumn();
			ImGui::SmallButton("View");
			if (ImGui::IsItemHovered() && s.uploaded) {
				ImGui::BeginTooltip();
				create_log_table(s);
				ImGui::EndTooltip();
			}
			ImGui::NextColumn();

			if (logs.size() < 9 && i == logs.size() - 1) {
				log_size.y = ImGui::GetCursorPosY();
			} else if (i == 9) {
				log_size.y = ImGui::GetCursorPosY();
			}
		}
		ImGui::Columns(1);
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
		// Upload Thread
		if (!upload_queue.empty())
		{
			ut_cv.notify_one();
		}
	}

	return uintptr_t();
}

void Uploader::create_log_table(const Log& l) {
	/*
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
	*/
}

void Uploader::start_async_refresh_log_list() {
	using namespace sqlite_orm;
	//Early out if we are already waiting on a refresh
	if (ft_file_list.valid()) return;

	ft_file_list = std::async(std::launch::async, [&](fs::path path) {
		std::vector<Log> file_list;
		auto filenames = storage->select(&Log::filename);
		std::set<std::string> filename_set(filenames.begin(), filenames.end());

		storage->begin_transaction();
		for (auto& p : fs::recursive_directory_iterator(path)) {
			if (fs::is_regular_file(p.status())) {
				auto& fn = p.path().filename().replace_extension().replace_extension().string();
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

void Uploader::parse_async_log(Log& aLog) {
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
	fs::path path = fs::path(aLog.path);
	//Attempt to extract. We assume that the zip extension is accurate and the archive contains an evtc file as its only entry
	if (path.extension().string() == ".zip" || path.extension().string() == ".zevtc") {
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
		if (log_data == nullptr) return;
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
		//aLog.parsed = log;
		delete parser;
	}
	free(log_data);
}

void Uploader::poll_async_refresh_log_list() {
	if (ft_file_list.valid()) {
		if (ft_file_list.wait_for(std::chrono::milliseconds(1)) == std::future_status::ready) {
			logs = ft_file_list.get();
		}
	}

	auto now = std::chrono::system_clock::now();
	auto diff = now - refresh_time;
	if (diff > std::chrono::minutes(5)) {
		start_async_refresh_log_list();
		refresh_time = now;
	}
}

void Uploader::start_upload_thread()
{
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
			response = cpr::Post(
				cpr::Url{"https://dps.report/uploadContent"},
				cpr::Multipart{ {"file", cpr::File{log->path.string()}}, {"json", "1"} }
				//cpr::Header{{"accept-encoding", "gzip, deflate"}}
			);

			StatusMessage status;
			if (response.status_code == 200) {
				json parsed = json::parse(response.text);
				LOG_F(INFO, "Header:");
				for (auto& val : response.header)
				{
					LOG_F(INFO, "%s : %s", val.first.c_str(), val.second.c_str());
				}
				LOG_F(INFO, "JSON: %s", response.text.c_str());
				
				log->uploaded = true;
				log->report_id = parsed["id"].get<std::string>();
				log->permalink = parsed["permalink"].get<std::string>();
				json encounter = parsed["encounter"];
				log->boss_id = encounter["bossId"].get<int>();
				log->boss_name = encounter["boss"].get<std::string>();
				log->json_available = encounter["jsonAvailable"].get<bool>();
				log->success = encounter["success"].get<bool>();
				
				status.msg = "Uploaded " + display + " - " + log->human_time + ".";
				status.url = parsed.value("permalink", "");
				status.encounter = log->boss_id;
			}
			else if (response.status_code == 401) {
				status.msg = "Upload failed. Invalid Username/Password. Please login again.";
			}
			else if (response.status_code == 400) {
				status.msg = "Upload failed. Invalid File/File Error or Connection Error.";
			}
			else {
				status.msg = "Unknown response.\n" + response.text;
				log->uploaded = true;
				log->error = true;
			}

			try
			{
				storage->update(*log);
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
