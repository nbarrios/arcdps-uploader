#include "Aleeva.h"
#include <future>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include "loguru.hpp"
#include "Settings.h"

using json = nlohmann::json;

bool Aleeva::login(Settings& settings) {
	if (settings.aleeva.access_code.empty()) {
		LOG_F(INFO, "Aleeva enabled but access code missing, skipping login.");
		return false;
	}

	if (Aleeva::authorize(settings)) {
		if (Aleeva::is_refresh_token_valid(settings)) {
			Aleeva::get_servers(settings);
			for (const Aleeva::DiscordId& server : settings.aleeva.server_ids) {
				Aleeva::get_channels(settings, server.id);
			}
			return true;
		}
	}

	return false;
}

bool Aleeva::authorize(Settings& settings)
{
	std::string grant_type = "access_code";

	if (is_refresh_token_valid(settings)) {
		grant_type = "refresh_token";
	}

	cpr::Response response;
	response = cpr::Post(
		cpr::Url{ "https://api.aleeva.io/auth/token" },
		cpr::Payload{
			{"grant_type", grant_type},
			{"client_id", "arc_dps_uploader"},
			{"client_secret", "9568468d-810a-4ce2-861e-e8011b658a28"},
			{"access_code", settings.aleeva.access_code},
			{"refresh_token", settings.aleeva.refresh_token},
			{"scopes", "report:write server:read channel:read"} });

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

				settings.save();

				settings.aleeva.authorised = true;
				return true;
			}
			catch (const json::exception& e) {
				LOG_F(ERROR, "Aleeva Auth JSON Parse Fail: %s", e.what());
			}
		}
	}
	else if (response.status_code == 401) {
		settings.aleeva.authorised = false;
		settings.aleeva.refresh_token = "";
		settings.aleeva.token_expiration = 0;
	}

	return false;
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

void Aleeva::get_servers(Settings& settings)
{
	if (!settings.aleeva.authorised) {
		return;
	}

	cpr::Response response;
	response = cpr::Get(
		cpr::Url{ "https://api.aleeva.io/server" },
		cpr::Bearer(settings.aleeva.api_key),
		cpr::Parameters{
			{"mode", "UPLOADS"}
		}
	);

	if (response.status_code == 200) {
		if (response.header.count("Content-Type") && response.header["Content-Type"] == "application/json") {
			try {
				json parsed = json::parse(response.text);
				for (auto& server : parsed) {
					DiscordId server_id;
					server_id.id = server["id"];
					server_id.name = server["name"];
					settings.aleeva.server_ids.push_back(server_id);
				}
				if (settings.aleeva.server_ids.size() > 0 && settings.aleeva.selected_server_id == "") {
					settings.aleeva.selected_server_id = settings.aleeva.server_ids[0].id;
				}
			}
			catch (const json::exception& e) {
				LOG_F(ERROR, "Aleeva Servers JSON Parse Fail: %s", e.what());
			}
		}
	}
	else {
		LOG_F(INFO, "Aleeva Servers response: %s", response.text.c_str());
	}
}

void Aleeva::get_channels(Settings& settings, const std::string& server_id) {
	if (!settings.aleeva.authorised) {
		return;
	}

	cpr::Response response;
	response = cpr::Get(
		cpr::Url{ "https://api.aleeva.io/server/" + server_id + "/channel" },
		cpr::Bearer{ settings.aleeva.api_key },
		cpr::Parameters{
			{"mode", "UPLOADS"}
		}
	);


	if (response.status_code == 200) {
		if (response.header.count("Content-Type") && response.header["Content-Type"] == "application/json") {
			try {
				json parsed = json::parse(response.text);
				for (auto& server : parsed) {
					DiscordId channel_id;
					channel_id.id = server["id"];
					channel_id.name = server["name"];

					if (settings.aleeva.channel_ids.count(server_id) == 0) {
						settings.aleeva.channel_ids.emplace(server_id, std::vector<DiscordId>());
					}

					auto& channels = settings.aleeva.channel_ids.at(server_id);
					channels.push_back(channel_id);

					if (channels.size() > 0 && settings.aleeva.selected_channel_id == "") {
						settings.aleeva.selected_channel_id = channels[0].id;
					}
				}
			}
			catch (const json::exception& e) {
				LOG_F(ERROR, "Aleeva Channels JSON Parse Fail: %s", e.what());
			}
		}
	}
	else {
		LOG_F(INFO, "Aleeva Channels response: %s", response.text.c_str());
	}
}

void Aleeva::post_log(AleevaSettings& settings, const std::string& log_path) {
	json body;
	body["sendNotification"] = settings.should_post;
	body["notificationServerId"] = settings.selected_server_id;
	body["notificationChannelId"] = settings.selected_channel_id;
	body["dpsReportPermalink"] = log_path;

	cpr::Response response;
	response = cpr::Post(
		cpr::Url{
			"https://api.aleeva.io/report" },
			cpr::Bearer{ settings.api_key },
			cpr::Header{
				{"accept", "application/json"},
				{"Content-Type", "application/json"},
		},
		cpr::Body{ body.dump() }
	);
	if (response.status_code != 200 && response.status_code != 201) {
		LOG_F(ERROR, "Aleeva post log failed: %s", response.text.c_str());
	}
}