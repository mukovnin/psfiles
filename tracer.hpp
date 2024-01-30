#pragma once

#include "event.hpp"
#include <codecvt>
#include <locale>
#include <signal.h>
#include <string>
#include <sys/types.h>

class Tracer {
private:
  pid_t pid{0};
  std::wstring cmdLine;
  std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
  bool withinSyscall{false};
  bool spawned{false}, attached{false};
  std::wstring closingFile;
  static sig_atomic_t terminate;
  EventCallback callback;
  bool iteration();
  bool waitForSyscall();
  bool spawnTracee(char *const *argv);
  bool setPtraceOptions();
  bool setSignalHandler();
  std::wstring filePath(int fd);
  std::wstring getCmdLine();
  static void signalHandler(int);

public:
  Tracer(pid_t pid, EventCallback cb);
  Tracer(char *const *argv, EventCallback cb);
  ~Tracer();
  bool loop();
  pid_t traceePid() const;
  std::wstring traceeCmdLine() const;
};
