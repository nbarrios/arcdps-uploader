#pragma once

#include "arcdps_defs.h"
#include "SimpleIni.h"
#include <cpr/cpr.h>
#include <filesystem>
#include <future>
#include <deque>
#include "Revtc.h"
#include "sqlite_orm.h"
#include "Log.h"

namespace fs = std::filesystem;

struct StatusMessage {
	std::string msg;
	std::string url;
	std::string encounter;
	uint64_t duration;
};

class Uploader
{
	bool ini_enabled;
	fs::path ini_path;
	CSimpleIniA ini;

	fs::path log_path;
	std::vector<Log> logs;
	std::future<decltype(logs)> ft_file_list;
	std::chrono::system_clock::time_point refresh_time;

	std::deque<int> upload_queue;
	std::vector<std::future<cpr::Response>> ft_uploads;

	std::future<cpr::Response> ft_auth_response;
	cpr::Response auth_response;

	std::vector<StatusMessage> status_messages;
	std::mutex ts_msg_mutex;
	std::vector<StatusMessage> thread_status_messages;

	std::thread upload_thread;
	std::atomic<bool> upload_thread_run;
	std::mutex ut_mutex;
	std::condition_variable ut_cv;

	void upload_thread_loop();
	void add_pending_upload_logs(std::vector<int>& queue);
	void poll_async_refresh_log_list();
public:
	bool is_open;
	bool in_combat;

	Uploader(fs::path data_path);
	~Uploader();

	uintptr_t imgui_tick();
	void create_log_table(const Log& l);

	void start_async_refresh_log_list();
	void parse_async_log(Log& aLog);

	void start_upload_thread();
};

