#pragma once

#include <cstddef>
#include <functional>
#include <string>

enum class Event { Open, Close, Read, Write };

struct EventInfo {
    Event type;
    std::string path;
    size_t arg {0};
};

using EventCallback = std::function<void(const EventInfo &)>;
