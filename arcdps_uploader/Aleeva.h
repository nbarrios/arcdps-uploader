#ifndef __ALEEVA_H__
#define __ALEEVA_H__

#include <string>

struct Settings;
struct AleevaSettings;

namespace Aleeva {
    struct DiscordId {
        std::string id;
        std::string name;
    };

    bool login(Settings& settings);

    bool authorize(Settings& settings);
    void deauthorize(Settings& settings);
    bool is_refresh_token_valid(Settings& settings);
    void get_servers(Settings& settings);
    void get_channels(Settings& settings, const std::string& server_id);

    void post_log(AleevaSettings& settings, const std::string& log_path);
}

#endif // __ALEEVA_H__
