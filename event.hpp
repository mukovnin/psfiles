#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <sys/types.h>

enum class Event { Open, Close, Read, Write, MMap };

struct EventInfo {
  pid_t pid;
  Event type;
  std::wstring path;
  size_t arg{0};
};

using EventCallback = std::function<void(const EventInfo &)>;
