#include "app.hpp"

#include <iostream>
#include <atomic>
#include <stdexcept>

#include "../utils/logger.hpp"
#include "../utils/string_utils.hpp"
#include "clipsync_build_config.hpp"
#include "../config.hpp"
#include "../platform/clipboard.hpp"
#include "../sync/sync_engine.hpp"

#ifdef _WIN32
#include <windows.h>
void requestClipboardMonitorStop();
namespace { BOOL WINAPI consoleHandler(DWORD type) { if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT || type == CTRL_BREAK_EVENT) { requestClipboardMonitorStop(); return TRUE; } return FALSE; } }

namespace {
// Hide the console window created for a background/Task Scheduler invocation.
void hideConsoleWindow() {
    if (HWND console = GetConsoleWindow(); console != nullptr) {
        // Hide immediately, then detach from the console.  Detaching allows
        // the console host (and its taskbar button) to exit when this process
        // was started by Task Scheduler with its own console.
        ShowWindow(console, SW_HIDE);
        FreeConsole();
    }
}
} // namespace
#endif

int App::run(int argc, char* argv[]) {
    std::vector<std::string> args(argv, argv + argc);

    // Handle display-related options before emitting startup logs so that a
    // scheduled task does not briefly flash a console window.
    bool hidden = false;
    for (const auto& arg : args) {
        if (arg == "--hidden") {
            hidden = true;
            break;
        }
    }
#ifdef _WIN32
    if (hidden) hideConsoleWindow();
#endif

    Logger::setLevel(Logger::Level::DEBUG);

    Logger::info("Starting " + std::string(PROJECT_NAME));

    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--version" || args[i] == "-v") {
            printVersion();
            return 0;
        }
        if (args[i] == "--help" || args[i] == "-h") {
            printHelp();
            return 0;
        }
    }

    std::string configPath = "clipsync.json";
    bool once = false;
    for (size_t i = 1; i + 1 < args.size(); ++i) if (args[i] == "--config") configPath = args[i + 1];
    for (const auto& arg : args) if (arg == "--once") once = true;
    try {
        auto config = AppConfig::load(configPath);
        SyncEngine engine(config);
        const bool synced = engine.startupSync();
        if (!synced) Logger::warning("startup sync failed");
        if (once) return synced ? 0 : 1;
#ifdef _WIN32
        if (!hidden && !SetConsoleCtrlHandler(consoleHandler, TRUE)) {
            Logger::warning("could not install console handler");
        }
#endif
        ClipboardMonitor monitor;
        engine.startPolling();
        Logger::info("ClipSync running");
        // The monitor owns the hidden window; consoleHandler posts WM_CLOSE to it.
        if (!monitor.run([&engine](const std::string& text) { engine.onClipboardChanged(text); })) {
            Logger::error("clipboard monitor stopped during initialization");
            engine.stop();
            return 1;
        }
        engine.stop();
        return 0;
    } catch (const std::exception& ex) {
        Logger::error(ex.what());
        return 1;
    }
}

void App::printVersion() const {
    std::cout << PROJECT_NAME << " v" << PROJECT_VERSION << std::endl;
}

void App::printHelp() const {
    std::cout << "Usage: " << PROJECT_NAME << " [options]\n"
              << "\nOptions:\n"
              << "  -h, --help     Show this help message\n"
              << "  -v, --version  Show version information\n"
              << "      --config   Path to JSON config (default: clipsync.json)\n"
              << "      --once     Run startup synchronization and exit\n"
              << "      --hidden   Hide the console window (for Task Scheduler)\n";
}
