#pragma once

#include "arcdps_defs.h"
#include "SimpleIni.h"
#include <cpr/cpr.h>
#include <filesystem>
#include <future>
#include <queue>
#include "Revtc.h"

namespace fs = std::filesystem;

struct Log {
	std::filesystem::path path;
	std::string filename;
	std::string human_time;
	std::chrono::system_clock::time_point time;
	Revtc::Log parsed;

	inline bool operator==(const Log&rhs) {
		return time == rhs.time && filename == rhs.filename;
	}

	friend inline bool operator>(const Log& lhs, const Log& rhs) {
		return lhs.time > rhs.time;
	}
};

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
	std::future<std::vector<std::string>> ft_file_list;
	std::vector<std::string> file_list;
	std::map<std::string, Log> cached_logs;

	std::vector<Log> pending_upload_queue;
	std::queue<Log> upload_queue;
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
public:
	bool is_open;

	Uploader();
	~Uploader();

	uintptr_t imgui_tick();
	void create_log_table(const Log& l);

	void start_async_refresh_log_list();
	void parse_async_log(Log& aLog);
	void poll_async_refresh_log_list();

	void add_pending_upload_logs(std::vector<Log>& queue, int category, std::string tags);
	void upload_thread_loop();
};

