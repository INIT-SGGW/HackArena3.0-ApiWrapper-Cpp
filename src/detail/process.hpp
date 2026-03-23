#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace hackarena3::detail {

struct ProcessResult {
    int exit_code {};
    std::string stdout_text;
    std::string stderr_text;
};

ProcessResult run_process(
    const std::filesystem::path& executable,
    const std::vector<std::string>& args
);

}  // namespace hackarena3::detail
