#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include "SimpleIni.h"
#include "Aleeva.h"

struct AleevaSettings {
	bool enabled;
	std::string access_code;
	bool authorised;
	std::string refresh_token;
	int64_t token_expiration;
	std::string api_key;
	std::vector<Aleeva::DiscordId> server_ids;
	std::string selected_server_id;
	std::map<std::string, std::vector<Aleeva::DiscordId>> channel_ids;
	std::string selected_channel_id;
	bool should_post;
	bool success_only;
};

struct Settings
{
	std::filesystem::path ini_path;
	CSimpleIniA ini;

	bool wvw_detailed_enabled;
	std::string msg_format;
	int recent_minutes;
	bool gw2bot_enabled;
	std::string gw2bot_key;
	bool gw2bot_success_only;
	AleevaSettings aleeva;

	Settings(std::filesystem::path& ini_path);
	void load();
	void save();
};

inline constexpr char* INI_SECTION_SETTINGS = "Settings";
inline constexpr char* INI_WVW_DETAILED_SETTING = "WvW_Detailed";
inline constexpr char* INI_MSG_FORMAT = "Msg_Format";
inline constexpr char* INI_RECENT_MINUTES = "Recent_Minutes";
inline constexpr char* INI_GW2BOT_ENABLED = "GW2Bot_Enabled";
inline constexpr char* INI_GW2BOT_KEY = "GW2Bot_Key";
inline constexpr char* INI_GW2BOT_SUCCESS_ONLY = "GW2Bot_Success_Only";
inline constexpr char* INI_ALEEVA_ENABLED = "Aleeva_Enabled";
inline constexpr char* INI_ALEEVA_REFRESH_TOKEN = "Aleeva_Refresh_Token";
inline constexpr char* INI_ALEEVA_TOKEN_EXPIRATION = "Aleeva_Token_Expiration";
inline constexpr char* INI_ALEEVA_SERVER_ID = "Aleeva_Server_Id";
inline constexpr char* INI_ALEEVA_CHANNEL_ID = "Aleeva_Channel_Id";
inline constexpr char* INI_ALEEVA_SHOULD_POST = "Aleeva_Should_Post";
inline constexpr char* INI_ALEEVA_SUCCESS_ONLY = "Aleeva_Success_Only";

#endif // __SETTINGS_H__
