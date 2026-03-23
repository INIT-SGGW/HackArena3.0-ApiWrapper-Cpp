#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace hackarena3::detail {

inline std::string trim(std::string_view value) {
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

inline std::string strip_matching_quotes(std::string_view value) {
    if (value.size() >= 2 && value.front() == value.back() &&
        (value.front() == '\'' || value.front() == '"')) {
        return std::string(value.substr(1, value.size() - 2));
    }
    return std::string(value);
}

}  // namespace hackarena3::detail
