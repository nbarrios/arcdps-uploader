#include "Settings.h"

#include "loguru.hpp"

Settings::Settings(std::filesystem::path& ini_path) : ini_path(ini_path) {}

void Settings::load() {
    ini.SetUnicode();
    SI_Error error = ini.LoadFile(ini_path.string().c_str());
    if (error == SI_OK) {
        LOG_F(INFO, "Loaded INI file");
        wvw_detailed_enabled = ini.GetBoolValue(
            INI_SECTION_SETTINGS, INI_WVW_DETAILED_SETTING, false);

        gw2bot_enabled =
            ini.GetBoolValue(INI_SECTION_SETTINGS, INI_GW2BOT_ENABLED, false);

        gw2bot_key = ini.GetValue(INI_SECTION_SETTINGS, INI_GW2BOT_KEY, "");

        gw2bot_success_only = ini.GetBoolValue(INI_SECTION_SETTINGS,
                                               INI_GW2BOT_SUCCESS_ONLY, false);

        aleeva.enabled =
            ini.GetBoolValue(INI_SECTION_SETTINGS, INI_ALEEVA_ENABLED, false);

        aleeva.refresh_token =
            ini.GetValue(INI_SECTION_SETTINGS, INI_ALEEVA_REFRESH_TOKEN, "");

        try {
            aleeva.token_expiration = std::stoll(ini.GetValue(
                INI_SECTION_SETTINGS, INI_ALEEVA_TOKEN_EXPIRATION, "0"));
        } catch (std::exception& e) {
            LOG_F(ERROR, "Failed to parse Aleeva token expiration: %s",
                  e.what());
        }
    }
}

void Settings::save() {
    ini.SetBoolValue(INI_SECTION_SETTINGS, INI_WVW_DETAILED_SETTING,
                     wvw_detailed_enabled);
    ini.SetBoolValue(INI_SECTION_SETTINGS, INI_GW2BOT_ENABLED, gw2bot_enabled);
    ini.SetValue(INI_SECTION_SETTINGS, INI_GW2BOT_KEY, gw2bot_key.c_str());
    ini.SetBoolValue(INI_SECTION_SETTINGS, INI_GW2BOT_SUCCESS_ONLY,
                     gw2bot_success_only);
    ini.SetBoolValue(INI_SECTION_SETTINGS, INI_ALEEVA_ENABLED, aleeva.enabled);
    ini.SetValue(INI_SECTION_SETTINGS, INI_ALEEVA_REFRESH_TOKEN,
                 aleeva.refresh_token.c_str());
    ini.SetValue(INI_SECTION_SETTINGS, INI_ALEEVA_TOKEN_EXPIRATION,
                 std::to_string(aleeva.token_expiration).c_str());
    SI_Error error = ini.SaveFile(ini_path.string().c_str());
    if (error != SI_OK) {
        LOG_F(ERROR, "Failed to save INI file");
    }
}