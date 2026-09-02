#pragma once

#include <string>

struct AppConfig {
    std::string davUrl;
    std::string remotePath;
    std::string username;
    std::string password;
    long pollIntervalMs = 3000;

    static AppConfig load(const std::string& path);
};
