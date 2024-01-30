#pragma once

#include <cstddef>
#include <functional>
#include <string>

enum class Event { Open, Close, Read, Write, MMap };

struct EventInfo {
  Event type;
  std::wstring path;
  size_t arg{0};
};

using EventCallback = std::function<void(const EventInfo &)>;
