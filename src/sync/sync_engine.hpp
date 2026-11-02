#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "../net/sync_clipboard.hpp"

class SyncEngine {
public:
    explicit SyncEngine(AppConfig config);
    ~SyncEngine();
    bool startupSync();
    void onClipboardChanged(const std::string& text);
    void startPolling();
    void stop();

private:
    SyncClipboardClient client_;
    long pollIntervalMs_;
    std::thread poller_;
    std::atomic<bool> stopping_{false};
    std::string lastTextHash_;
    std::mutex stateMutex_;
    std::mutex waitMutex_;
    std::condition_variable wake_;
};
