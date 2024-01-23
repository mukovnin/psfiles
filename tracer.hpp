#pragma once

#include "event.hpp"
#include <signal.h>
#include <string>
#include <sys/types.h>

class Tracer {
private:
  pid_t pid{0};
  bool withinSyscall{false};
  bool spawned{false}, attached{false};
  std::string closingFile;
  static sig_atomic_t terminate;
  EventCallback callback;
  bool iteration();
  bool waitForSyscall();
  bool spawnTracee(char *const *argv);
  bool setPtraceOptions();
  bool setSignalHandler();
  std::string filePath(int fd);
  static void signalHandler(int);

public:
  Tracer(pid_t pid, EventCallback cb);
  Tracer(char *const *argv, EventCallback cb);
  ~Tracer();
  bool loop();
};
