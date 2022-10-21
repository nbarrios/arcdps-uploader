// arcdps_uploader.cpp : Defines the exported functions for the DLL application.
//

#include "arcdps_uploader.h"
#include "Uploader.h"
#include "imgui.h"
#include <codecvt>
#include <fstream>
#include <chrono>
#include <iomanip>
#include "loguru.hpp"

static char* arcvers;
static arcdps_exports exports;
static Uploader* up;

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

/* initialize mod -- return table that arcdps will use for callbacks */
arcdps_exports* mod_init() {
	int argc = 1;
	char* argv[] = { "uploader.log", nullptr };
	loguru::init(argc, argv);

	const fs::path uploader_data_path = "./addons/uploader/";
	if (!fs::exists(uploader_data_path))
	{
		fs::create_directory(uploader_data_path);
	}

	//Loguru Log
	fs::path log_path = uploader_data_path / "uploader.log";
	loguru::add_file(log_path.string().c_str(), loguru::Truncate, loguru::Verbosity_MAX);

	//Uploader
	up = new Uploader(uploader_data_path);
	up->start_async_refresh_log_list();
	up->start_upload_thread();

	/* for arcdps */
	exports.size = sizeof(arcdps_exports);
	exports.sig = 0x92485179;
	exports.imguivers = IMGUI_VERSION_NUM;
	exports.out_name = "uploader";
	exports.out_build = "0.9.6";
	exports.wnd_nofilter = mod_wnd;
	exports.combat = mod_combat;
	exports.imgui = mod_imgui;
	return &exports;
}

/* release mod -- return ignored */
uintptr_t mod_release() {
	delete up;
	return 0;
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
		up->is_open = !up->is_open;
		LOG_F(INFO, "Window Toggle: %i", up->is_open);
	}

	return uMsg;
}

/* combat callback -- may be called asynchronously. return ignored */
/* one participant will be party/squad, or minijpon of. no spawn statechange events. despawn statechange only on marked boss npcs */
uintptr_t mod_combat(cbtevent* ev, ag* src, ag* dst, char* skillname, uint64_t id, uint64_t revision)
{
	if (ev)
	{
		if (src && src->self)
		{
			if (ev->is_statechange == CBTS_ENTERCOMBAT) {
				up->in_combat = true;
			}
			else if (ev->is_statechange == CBTS_EXITCOMBAT)
			{
				up->in_combat = false;
			}
		}
	}
	return uintptr_t();
}

uintptr_t mod_imgui() {
	return up->imgui_tick();
}

