#ifndef __ALEEVA_H__
#define __ALEEVA_H__

#include <string>
#include "Settings.h"

namespace Aleeva {
    std::string authorize(Settings& settings);
    void deauthorize(Settings& settings);
    bool is_refresh_token_valid(Settings& settings);
}

#endif // __ALEEVA_H__
