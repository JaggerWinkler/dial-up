#include "console.h"

#include <cstdio>
#include <iostream>
#include <mutex>

namespace console {
namespace {
std::mutex g_mutex;
}

void out(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::cout << line << std::endl;
}

void err(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::cerr << line << std::endl;
}

void prompt() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::cout << "> " << std::flush;
}

std::string macToString(const MAC_Address& mac) {
    char s[18];
    std::snprintf(s, sizeof s, "%02x:%02x:%02x:%02x:%02x:%02x",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return s;
}

}  // namespace console
