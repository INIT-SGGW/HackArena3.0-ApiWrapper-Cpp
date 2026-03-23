#include "detail/environment.hpp"

#include <cstdlib>

#ifdef _WIN32
#include <stdlib.h>
#else
#include <unistd.h>
#endif

namespace hackarena3::detail {

std::optional<std::string> get_env(const char* key) {
    if (const char* value = std::getenv(key)) {
        return std::string(value);
    }
    return std::nullopt;
}

void set_env_if_unset(const std::string& key, const std::string& value) {
    if (get_env(key.c_str()).has_value()) {
        return;
    }

#ifdef _WIN32
    _putenv_s(key.c_str(), value.c_str());
#else
    setenv(key.c_str(), value.c_str(), 0);
#endif
}

}  // namespace hackarena3::detail
