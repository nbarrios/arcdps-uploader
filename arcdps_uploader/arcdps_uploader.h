#pragma once

#include "arcdps_defs.h"
#include "SimpleIni.h"
#include <cpr/cpr.h>
#include <filesystem>
#include <future>
#include <queue>
#include "Revtc.h"

namespace fs = std::experimental::filesystem;

enum class Destination {
	GW2RAIDAR,
	DPSREPORT
};

struct Log {
	fs::path path;
	std::string filename;
	std::string human_time;
	fs::file_time_type time;
	int category;
	std::string tags;
	std::string auth_token;
	Revtc::Log parsed;
	Destination dest;

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

/* proto/globals */
uint32_t cbtcount = 0;
arcdps_exports arc_exports;
char* arcvers;

bool is_open;

bool ini_enabled;
fs::path ini_path;
CSimpleIniA ini;

bool cred_save;
char username_buf[64] = "";
char pass_buf[64] = "";
bool authentication_in_progress;
bool is_authenticated;
std::string auth_token;

fs::path log_path;
std::future<std::vector<Log>> ft_file_list;
std::vector<Log> file_list;

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

void dll_init(HANDLE hModule);
void dll_exit();
extern "C" __declspec(dllexport) void* get_init_addr(char* arcversionstr, void* imguicontext);
extern "C" __declspec(dllexport) void* get_release_addr();
arcdps_exports* mod_init();
uintptr_t mod_release();
uintptr_t mod_wnd(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
uintptr_t mod_combat(cbtevent* ev, ag* src, ag* dst, char* skillname);
uintptr_t mod_imgui();
void create_log_table(const Log& l);

void start_async_refresh_log_list();
void parse_async_log(Log& aLog);
void poll_async_refresh_log_list();

void start_async_authentication();
void poll_async_authentication();

void add_pending_upload_logs(std::vector<Log>& queue, Destination dest, int category, std::string tags);
void upload_thread_loop();