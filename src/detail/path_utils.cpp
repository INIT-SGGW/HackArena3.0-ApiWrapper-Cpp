#include "detail/path_utils.hpp"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "detail/environment.hpp"
#include "detail/string_utils.hpp"

namespace {

std::vector<std::string> split_string(const std::string& value, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream stream(value);
    std::string part;
    while (std::getline(stream, part, delimiter)) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    return parts;
}

bool has_path_separator(const std::string& value) {
    return value.find('/') != std::string::npos || value.find('\\') != std::string::npos;
}

std::optional<std::filesystem::path> resolve_existing_file(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
        return std::filesystem::absolute(path, ec);
    }
    return std::nullopt;
}

std::vector<std::filesystem::path> executable_variants(const std::filesystem::path& path) {
    std::vector<std::filesystem::path> variants;
    variants.push_back(path);

#ifdef _WIN32
    if (!path.has_extension()) {
        auto pathext = hackarena3::detail::get_env("PATHEXT").value_or(".EXE;.CMD;.BAT;.COM");
        for (const auto& ext : split_string(pathext, ';')) {
            auto normalized = ext;
            if (!normalized.empty() && normalized.front() != '.') {
                normalized.insert(normalized.begin(), '.');
            }
            variants.emplace_back(path.string() + normalized);
        }
    }
#endif

    return variants;
}

}  // namespace

namespace hackarena3::detail {

std::filesystem::path expand_user_path(const std::string& value) {
    if (value.empty() || value.front() != '~') {
        return std::filesystem::path(value);
    }
    if (value.size() > 1 && value[1] != '/' && value[1] != '\\') {
        return std::filesystem::path(value);
    }

    auto home = get_env("USERPROFILE");
    if (!home.has_value()) {
        home = get_env("HOME");
    }
    if (!home.has_value() || home->empty()) {
        return std::filesystem::path(value);
    }

    std::filesystem::path path(*home);
    if (value.size() > 2) {
        path /= value.substr(2);
    }
    return path;
}

std::optional<std::filesystem::path> resolve_executable_candidate(const std::string& candidate) {
    if (candidate.empty()) {
        return std::nullopt;
    }

    const auto expanded = expand_user_path(candidate);
    for (const auto& variant : executable_variants(expanded)) {
        if (auto resolved = resolve_existing_file(variant)) {
            return resolved;
        }
    }

    if (expanded.is_absolute() || expanded.has_parent_path() || has_path_separator(candidate)) {
        return std::nullopt;
    }

#ifdef _WIN32
    constexpr char path_separator = ';';
#else
    constexpr char path_separator = ':';
#endif

    for (const auto& segment : split_string(get_env("PATH").value_or(""), path_separator)) {
        const auto dir = expand_user_path(trim(segment));
        for (const auto& variant : executable_variants(dir / candidate)) {
            if (auto resolved = resolve_existing_file(variant)) {
                return resolved;
            }
        }
    }

    return std::nullopt;
}

bool stdin_is_tty() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

}  // namespace hackarena3::detail
