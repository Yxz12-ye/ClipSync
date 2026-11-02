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

class SyncClipboardClient {
public:
    explicit SyncClipboardClient(AppConfig config);
    bool push(const std::string& text);
    std::optional<Profile> pull();
    void resetRevision();

private:
    AppConfig config_;
    std::string etag_;
    mutable std::mutex mutex_;
    std::string fileUrl() const;
};

std::string textHash(const std::string& text);
