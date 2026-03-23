#include "detail/process.hpp"

#include <array>
#include <string>
#include <thread>
#include <vector>

#include "hackarena3/errors.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32

std::string quote_windows_argument(const std::string& argument) {
    if (argument.empty()) {
        return "\"\"";
    }

    const auto needs_quotes = argument.find_first_of(" \t\n\v\"") != std::string::npos;
    if (!needs_quotes) {
        return argument;
    }

    std::string quoted = "\"";
    std::size_t backslash_count = 0;
    for (const char ch : argument) {
        if (ch == '\\') {
            ++backslash_count;
            continue;
        }
        if (ch == '"') {
            quoted.append(backslash_count * 2 + 1, '\\');
            quoted.push_back('"');
            backslash_count = 0;
            continue;
        }
        quoted.append(backslash_count, '\\');
        quoted.push_back(ch);
        backslash_count = 0;
    }
    quoted.append(backslash_count * 2, '\\');
    quoted.push_back('"');
    return quoted;
}

std::string read_handle_to_string(HANDLE handle) {
    std::string output;
    std::array<char, 4096> buffer {};
    DWORD bytes_read = 0;
    while (ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr) &&
           bytes_read > 0) {
        output.append(buffer.data(), bytes_read);
    }
    return output;
}

#else

std::string read_fd_to_string(int fd) {
    std::string output;
    std::array<char, 4096> buffer {};
    for (;;) {
        const auto bytes_read = ::read(fd, buffer.data(), buffer.size());
        if (bytes_read <= 0) {
            break;
        }
        output.append(buffer.data(), static_cast<std::size_t>(bytes_read));
    }
    return output;
}

#endif

}  // namespace

namespace hackarena3::detail {

ProcessResult run_process(
    const std::filesystem::path& executable,
    const std::vector<std::string>& args
) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES security_attributes {};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.lpSecurityDescriptor = nullptr;
    security_attributes.bInheritHandle = TRUE;

    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    HANDLE stderr_read = nullptr;
    HANDLE stderr_write = nullptr;

    if (!CreatePipe(&stdout_read, &stdout_write, &security_attributes, 0) ||
        !SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0) ||
        !CreatePipe(&stderr_read, &stderr_write, &security_attributes, 0) ||
        !SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0)) {
        throw RuntimeError("Failed to create process pipes.");
    }

    STARTUPINFOA startup_info {};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup_info.hStdOutput = stdout_write;
    startup_info.hStdError = stderr_write;

    PROCESS_INFORMATION process_info {};

    std::string command_line = quote_windows_argument(executable.string());
    for (const auto& arg : args) {
        command_line.push_back(' ');
        command_line += quote_windows_argument(arg);
    }

    std::vector<char> command_buffer(command_line.begin(), command_line.end());
    command_buffer.push_back('\0');

    if (!CreateProcessA(
            executable.string().c_str(),
            command_buffer.data(),
            nullptr,
            nullptr,
            TRUE,
            0,
            nullptr,
            nullptr,
            &startup_info,
            &process_info
        )) {
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        CloseHandle(stderr_read);
        CloseHandle(stderr_write);
        throw RuntimeError("Failed to launch process: " + executable.string());
    }

    CloseHandle(stdout_write);
    CloseHandle(stderr_write);

    std::string stdout_text;
    std::string stderr_text;
    std::thread stdout_reader([&] { stdout_text = read_handle_to_string(stdout_read); });
    std::thread stderr_reader([&] { stderr_text = read_handle_to_string(stderr_read); });

    WaitForSingleObject(process_info.hProcess, INFINITE);

    DWORD exit_code = 0;
    GetExitCodeProcess(process_info.hProcess, &exit_code);

    stdout_reader.join();
    stderr_reader.join();

    CloseHandle(stdout_read);
    CloseHandle(stderr_read);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);

    return ProcessResult {
        .exit_code = static_cast<int>(exit_code),
        .stdout_text = std::move(stdout_text),
        .stderr_text = std::move(stderr_text),
    };
#else
    int stdout_pipe[2] {-1, -1};
    int stderr_pipe[2] {-1, -1};
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        if (stdout_pipe[0] != -1) {
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
        }
        if (stderr_pipe[0] != -1) {
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
        }
        throw RuntimeError("Failed to create process pipes.");
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        throw RuntimeError("Failed to fork child process.");
    }

    if (pid == 0) {
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);

        std::vector<std::string> args_copy = args;
        std::vector<char*> argv;
        argv.reserve(args_copy.size() + 2);
        std::string executable_text = executable.string();
        argv.push_back(executable_text.data());
        for (auto& arg : args_copy) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);
        execv(executable.c_str(), argv.data());
        _exit(127);
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    std::string stdout_text;
    std::string stderr_text;
    std::thread stdout_reader([&] { stdout_text = read_fd_to_string(stdout_pipe[0]); });
    std::thread stderr_reader([&] { stderr_text = read_fd_to_string(stderr_pipe[0]); });

    int status = 0;
    waitpid(pid, &status, 0);

    stdout_reader.join();
    stderr_reader.join();

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    int exit_code = 0;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_code = 128 + WTERMSIG(status);
    }

    return ProcessResult {
        .exit_code = exit_code,
        .stdout_text = std::move(stdout_text),
        .stderr_text = std::move(stderr_text),
    };
#endif
}

}  // namespace hackarena3::detail
