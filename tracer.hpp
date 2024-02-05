#pragma once

#include "event.hpp"
#include <codecvt>
#include <locale>
#include <map>
#include <set>
#include <signal.h>
#include <string>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>

class Tracer {
private:
  static constexpr int options{PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACECLONE};
  pid_t mainPid{0};
  std::wstring cmdLine;
  std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
  std::map<pid_t, bool> withinSyscall;
  bool spawned{false}, attached{false};
  std::map<pid_t, std::wstring> closingFiles;
  static sig_atomic_t terminate;
  EventCallback callback;
  bool iteration();
  bool handleSyscall(pid_t tid);
  bool spawnTracee(char *const *argv);
  bool setSignalHandler();
  std::set<pid_t> getProcThreads();
  std::wstring filePath(int fd);
  std::wstring getCmdLine();
  static void signalHandler(int);

public:
  Tracer(pid_t pid, EventCallback cb);
  Tracer(char *const *argv, EventCallback cb);
  Tracer(const Tracer &) = delete;
  Tracer &operator=(const Tracer &) = delete;
  Tracer(Tracer &&) = delete;
  Tracer &operator=(Tracer &&) = delete;
  ~Tracer();
  bool loop();
  pid_t traceePid() const;
  std::wstring traceeCmdLine() const;
};
