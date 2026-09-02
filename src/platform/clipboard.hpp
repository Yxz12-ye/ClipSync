#pragma once

#include <functional>
#include <string>

class ClipboardMonitor {
public:
    using Callback = std::function<void(const std::string&)>;
    bool run(const Callback& callback);
    static bool setText(const std::string& text);
    static std::string getText();
};
