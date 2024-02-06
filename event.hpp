#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <sys/types.h>

enum class Event { Open, Close, Read, Write, Map, Rename, Unlink };

struct EventInfo {
  pid_t pid;
  Event type;
  std::wstring path;
  size_t sizeArg{0};
  std::wstring strArg{};
};

using EventCallback = std::function<void(const EventInfo &)>;
