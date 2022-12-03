#include "Aleeva.h"
#include <future>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include "loguru.hpp"

using json = nlohmann::json;

std::string Aleeva::authorize(Settings& settings)
{
    std::string grant_type = "access_code";

    if (is_refresh_token_valid(settings)) {
        grant_type = "refresh_token";
    }

    cpr::Response response;
    response = cpr::Post(
        cpr::Url{"https://api.aleeva.io/auth/token"},
        cpr::Payload{
            {"grant_type", grant_type},
            {"client_id", "arc_dps_uploader"},
            {"client_secret", "9568468d-810a-4ce2-861e-e8011b658a28"},
            {"access_code", settings.aleeva.access_code},
            {"refresh_token", settings.aleeva.refresh_token},
            {"scopes", "report:write server:read channel:read"}});

    LOG_F(INFO, "Aleeva Auth response: %s", response.text.c_str());

    if (response.status_code == 200) {
        if (response.header.count("Content-Type") && response.header["Content-Type"] == "application/json") {
            try {
                json parsed = json::parse(response.text);
                settings.aleeva.api_key = parsed["accessToken"];
                settings.aleeva.refresh_token = parsed["refreshToken"];

                auto now = time(nullptr);
                int64_t expires_in = parsed["refreshExpiresIn"];
                settings.aleeva.token_expiration = now + expires_in;

                settings.aleeva.authorised = true;
            } catch(const json::exception& e) {
                LOG_F(ERROR, "Aleeva Auth JSON Parse Fail: %s", e.what());
            }
        }
    }

    return "Aleeva login failed. See log for details.";
}

void Aleeva::deauthorize(Settings& settings) {
    settings.aleeva.authorised = false;
    settings.aleeva.refresh_token = "";
    settings.aleeva.token_expiration = 0;
}

bool Aleeva::is_refresh_token_valid(Settings& settings)
{
    if (settings.aleeva.refresh_token.length() == 0) {
        return false;
    }

    time_t now = time(nullptr);
    if (now > settings.aleeva.token_expiration - 60) {
        return false;
    }

    return true;
}