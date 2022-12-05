#ifndef __ALEEVA_H__
#define __ALEEVA_H__

#include <string>

struct Settings;

namespace Aleeva {
    struct DiscordId {
        std::string id;
        std::string name;
    };

    void login(Settings& settings);

    std::string authorize(Settings& settings);
    void deauthorize(Settings& settings);
    bool is_refresh_token_valid(Settings& settings);
    void get_servers(Settings& settings);
    void get_channels(Settings& settings, const std::string& server_id);
}

#endif // __ALEEVA_H__
