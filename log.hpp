#pragma once

#include <chrono>
#include <cstring>
#include <errno.h>
#include <iomanip>
#include <iostream>
#include <sstream>

enum LogType { LogInfo, LogWarning, LogError };

#define LOG(lvl, fmt, ...) logImpl(__FILE__, __LINE__, lvl, fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) LOG(LogInfo, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) LOG(LogWarning, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) LOG(LogError, fmt, ##__VA_ARGS__)
#define LOGPE(syscall) LOGE(#syscall ": error # (#).", errno, strerror(errno))

static const char *logTypes[] = {"INFO", "WARN", "ERR"};

static constexpr const char *extractFileName(const char *path) {
  const char *p = path;
  while (*path)
    if (*path++ == '/')
      p = path;
  return p;
}

static inline void logLine(std::ostringstream &str, const char *fmt) {
  str << fmt;
}

template <typename T, typename... Ts>
void logLine(std::ostringstream &str, const char *fmt, T &&val, Ts &&...args) {
  while (*fmt) {
    if (*fmt == '#') {
      str << val;
      logLine(str, fmt + 1, args...);
      return;
    }
    str << *fmt++;
  }
}

template <typename... Ts>
void logImpl(const char *path, int line, LogType type, const char *fmt,
             Ts &&...args) {
  std::ostringstream s;
  auto time =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  auto localtime = std::localtime(&time);
  s << "[" << std::put_time(localtime, "%F %X") << "] ";
  s << "[" << std::setw(4) << logTypes[type] << "] ";
  s << "[" << std::setw(10) << extractFileName(path) << ": " << std::setw(3)
    << line << "] ";
  logLine(s, fmt, args...);
  std::cerr << s.str() << std::endl;
}
