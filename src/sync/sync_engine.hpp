#pragma once

#include <atomic>
#include <mutex>
#include <thread>

#include "../net/webdav.hpp"

class SyncEngine {
public:
    explicit SyncEngine(AppConfig config);
    ~SyncEngine();
    bool startupSync();
    void onClipboardChanged(const std::string& text);
    void startPolling();
    void stop();

private:
    WebDavClient client_;
    long pollIntervalMs_;
    std::thread poller_;
    std::atomic<bool> stopping_{false};
    std::string lastTextHash_;
    std::string suppressedHash_;
    std::mutex stateMutex_;
};
