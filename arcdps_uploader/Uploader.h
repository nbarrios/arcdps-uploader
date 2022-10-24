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

struct StatusMessage
{
	std::string msg;
	int log_id;
};

struct UserToken
{
	int id;
	std::string value;
	bool disabled;
	char value_buf[128];
};

struct Webhook
{
	int id;
	std::string name;
	std::string url;
	bool raids;
	bool fractals;
	bool strikes;
	bool golems;
	bool wvw;
	std::string filter;
	int filter_min;
	bool success;

	char name_buf[64];
	char url_buf[192];
	char filter_buf[128];
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
	std::vector<UserToken> userTokens;
	UserToken userToken;
	std::vector<Webhook> webhooks;
	std::mutex wh_mutex;
	std::deque<int> wh_queue;

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

	Uploader(fs::path data_path, std::optional<fs::path> custom_log_path);
	~Uploader();

	uintptr_t imgui_tick();
	void create_log_table(Log& l);
	
	void check_webhooks(int log_id);

	void start_async_refresh_log_list();

	void start_upload_thread();
};

