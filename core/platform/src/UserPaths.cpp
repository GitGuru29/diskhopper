#include "diskhopper/platform/UserPaths.hpp"

#include <cstdlib>
#include <string>

namespace diskhopper {

std::filesystem::path home_directory() {
    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0') {
        return std::filesystem::path(home);
    }
    return std::filesystem::path();
}

std::filesystem::path expand_home(const std::filesystem::path& input,
                                  const std::filesystem::path& home) {
    if (input.empty() || home.empty()) return input;
    if (input == "~") return home;
    std::string text = input.string();
    if (text == "~/" || text.rfind("~/", 0) == 0) {
        return home / text.substr(2);
    }
    return input;
}

}