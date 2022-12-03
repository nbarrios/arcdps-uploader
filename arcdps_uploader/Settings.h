#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <cstdint>

struct AleevaSettings {
	bool enabled;
	std::string access_code;
	bool authorised;
	std::string refresh_token;
	int64_t token_expiration;
	std::string api_key;
	std::vector<int64_t> server_ids;
	int64_t selected_server_id;
	std::vector<int64_t> channel_ids;
	int64_t selected_channel_id;
};

struct Settings
{
	bool wvw_detailed_enabled;
	bool gw2bot_enabled;
	std::string gw2bot_key;
	bool gw2bot_success_only;
	AleevaSettings aleeva;
};

inline constexpr char* INI_SECTION_SETTINGS = "Settings";
inline constexpr char* INI_WVW_DETAILED_SETTING = "WvW_Detailed";
inline constexpr char* INI_GW2BOT_ENABLED = "GW2Bot_Enabled";
inline constexpr char* INI_GW2BOT_KEY = "GW2Bot_Key";
inline constexpr char* INI_GW2BOT_SUCCESS_ONLY = "GW2Bot_Success_Only";
inline constexpr char* INI_ALEEVA_ENABLED = "Aleeva_Enabled";
inline constexpr char* INI_ALEEVA_REFRESH_TOKEN = "Aleeva_Refresh_Token";
inline constexpr char* INI_ALEEVA_TOKEN_EXPIRATION = "Aleeva_Token_Expiration";

#endif // __SETTINGS_H__
