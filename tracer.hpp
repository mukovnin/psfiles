#pragma once

#include "event.hpp"
#include <cstdint>
#include <map>
#include <set>
#include <signal.h>
#include <string>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>

class Tracer {
private:
  struct SyscallState {
    uint64_t nr;
    uint64_t args[6];
  };
  static constexpr int options{PTRACE_O_TRACESYSGOOD | PTRACE_O_TRACECLONE};
  static constexpr const char *invalidFd{"*INVALID FD*"};
  pid_t mainPid{0};
  std::string cmdLine;
  std::map<pid_t, SyscallState> state;
  bool spawned{false}, attached{false};
  int lastErr{0};
  std::map<pid_t, std::string> closingFiles;
  static sig_atomic_t terminate;
  EventCallback callback;
  bool iteration();
  bool handleSyscall(pid_t tid);
  bool spawnTracee(char *const *argv);
  bool setSignalHandler();
  std::set<pid_t> getProcThreads();
  std::string filePath(int fd);
  std::string filePath(int dirFd, const std::string &relPath);
  std::string fixRelativePath(const std::string &path);
  std::string getCmdLine();
  std::string readLink(const std::string &path);
  std::string readString(pid_t tid, void *addr);
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
  std::string traceeCmdLine() const;
};
