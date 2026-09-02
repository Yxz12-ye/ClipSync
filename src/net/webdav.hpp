#pragma once

#include <optional>
#include <mutex>
#include <string>

#include "../config.hpp"

struct Profile {
    std::string text;
    std::string hash;
    std::string etag;
};

class WebDavClient {
public:
    explicit WebDavClient(AppConfig config);
    bool push(const std::string& text);
    std::optional<Profile> pull();
    bool testConnection(std::string& error);

private:
    AppConfig config_;
    std::string etag_;
    mutable std::mutex mutex_;
    std::string fileUrl() const;
    std::string directoryUrl() const;
    bool makeDirectories();
};

std::string textHash(const std::string& text);
