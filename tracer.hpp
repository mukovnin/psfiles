#pragma once

#include <signal.h>
#include <string>
#include <sys/types.h>

class Tracer {
private:
  pid_t pid{0};
  bool withinSyscall{false};
  bool spawned{false}, attached{false};
  static sig_atomic_t terminate;
  bool iteration();
  bool waitForSyscall();
  bool spawnTracee(char *const *argv);
  bool setPtraceOptions();
  bool setSignalHandler();
  static void signalHandler(int);

public:
  Tracer(pid_t pid);
  Tracer(char *const *argv);
  ~Tracer();
  bool loop();
};
