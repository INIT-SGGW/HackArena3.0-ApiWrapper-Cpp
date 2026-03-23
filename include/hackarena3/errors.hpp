#pragma once

#include <stdexcept>
#include <string>

namespace hackarena3 {

class RuntimeError : public std::runtime_error {
public:
    explicit RuntimeError(const std::string& message);
};

class ConfigError : public RuntimeError {
public:
    explicit ConfigError(const std::string& message);
};

class AuthError : public RuntimeError {
public:
    explicit AuthError(const std::string& message);
};

class GameTokenError : public RuntimeError {
public:
    explicit GameTokenError(const std::string& message);
};

}  // namespace hackarena3
