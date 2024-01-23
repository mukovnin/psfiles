#include "tracer.hpp"
#include "log.hpp"
#include <algorithm>
#include <asm/unistd.h>
#include <cstdint>
#include <ctype.h>
#include <errno.h>
#include <fstream>
#include <iterator>
#include <linux/limits.h>
#include <stdlib.h>
#include <string>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

sig_atomic_t Tracer::terminate{0};

Tracer::Tracer(pid_t pid, EventCallback cb) : pid(pid), callback(cb) {
  if (!setSignalHandler())
    return;
  if (ptrace(PTRACE_ATTACH, pid, nullptr, nullptr) != 0) {
    LOGPE("ptrace (ATTACH)");
    return;
  }
  attached = true;
  cmdLine = getCmdLine();
  if (setPtraceOptions())
    LOGI("Attached to process with PID #.", pid);
}

Tracer::Tracer(char *const *argv, EventCallback cb) : callback(cb) {
  if (!setSignalHandler())
    return;
  pid = fork();
  if (pid < 0) {
    LOGPE("fork");
  } else if (pid == 0) {
    spawnTracee(argv);
  } else {
    spawned = true;
    int status{0};
    if (waitpid(pid, &status, 0) < 0) {
      LOGPE("waitpid");
      return;
    }
    if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGSTOP) {
      LOGE("Unexpected wait status: #0x#\n", std::hex, status);
      return;
    }
    cmdLine = getCmdLine();
    if (setPtraceOptions())
      LOGI("Tracee successfully started (PID #).", pid);
  }
}

Tracer::~Tracer() {
  if (spawned) {
    kill(pid, SIGTERM);
    LOGI("Sent SIGTERM to tracee (PID #).", pid);
  } else if (attached) {
    kill(pid, SIGSTOP);
    waitpid(pid, nullptr, 0);
    if (ptrace(PTRACE_DETACH, pid, nullptr, nullptr) == 0)
      LOGI("Detached from process with PID #.", pid);
    else
      LOGPE("ptrace (DETACH)");
    kill(pid, SIGCONT);
  }
}

bool Tracer::setPtraceOptions() {
  if (attached) {
    kill(pid, SIGSTOP);
    waitpid(pid, nullptr, 0);
  }
  if (ptrace(PTRACE_SETOPTIONS, pid, nullptr, PTRACE_O_TRACESYSGOOD) < 0) {
    LOGPE("ptrace (SETOPTIONS)");
    return false;
  }
  if (attached) {
    kill(pid, SIGCONT);
  }
  return true;
}

std::string Tracer::filePath(int fd) {
  const char *invalid = "*INVALID FD*";
  if (pid <= 0)
    return {};
  if (fd < 0)
    return invalid;
  const char *std[] = {"*STDIN*", "*STDOUT*", "*STDERR*"};
  if (fd <= 2)
    return std[fd];
  std::string linkPath =
      "/proc/" + std::to_string(pid) + "/fd/" + std::to_string(fd);
  std::string out(PATH_MAX, 0);
  if (readlink(linkPath.data(), out.data(), out.size()) == -1) {
    LOGPE("readlink");
    return invalid;
  }
  return out;
}

std::string Tracer::getCmdLine() {
  std::string path = "/proc/" + std::to_string(pid) + "/cmdline";
  std::ifstream file(path);
  if (!file)
    return {};
  std::istreambuf_iterator<char> beg{file}, end;
  std::string str;
  std::transform(beg, end, std::back_inserter(str),
                 [](char c) { return c ? c : ' '; });
  return str;
}

void Tracer::signalHandler(int) { terminate = 1; }

bool Tracer::setSignalHandler() {
  struct sigaction act;
  sigemptyset(&act.sa_mask);
  act.sa_handler = &Tracer::signalHandler;
  act.sa_flags = 0;
  if (sigaction(SIGINT, &act, nullptr) == 0 &&
      sigaction(SIGTERM, &act, nullptr) == 0) {
    return true;
  }
  LOGPE("sigaction");
  return false;
}

bool Tracer::spawnTracee(char *const *argv) {
  if (ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) < 0) {
    LOGPE("ptrace (TRACEME)");
    return false;
  }
  if (raise(SIGSTOP)) {
    LOGPE("raise (SIGSTOP)");
    return false;
  }
  if (execvp(argv[0], argv) < 0) {
    LOGPE("execvp");
    return false;
  }
  return true;
}

bool Tracer::iteration() {
  if (!waitForSyscall())
    return false;

  struct user_regs_struct regs;

  if (ptrace(PTRACE_GETREGS, pid, 0, &regs) < 0) {
    LOGPE("ptrace (GETREGS)");
    return false;
  }
  // Syscall enter:
  //  orig_rax - syscall number
  //       rdi - fd (read, write, close)
  // Syscall exit:
  //  orig_rax - syscall number
  //       rdi - fd (read, write, close)
  //       rax - read/write count (read, write)
  int64_t sc = regs.orig_rax, rax = regs.rax, rdi = regs.rdi;

  if (!withinSyscall) {
    if (sc == __NR_close)
      closingFile = filePath(rdi);
  } else if (rax >= 0) {
    switch (sc) {
    case __NR_read:
    case __NR_readv: {
      callback(EventInfo{Event::Read, filePath(rdi), (size_t)rax});
      break;
    }
    case __NR_write:
    case __NR_writev: {
      callback(EventInfo{Event::Write, filePath(rdi), (size_t)rax});
      break;
    }
    case __NR_open:
    case __NR_openat: {
      callback(EventInfo{Event::Open, filePath(rax)});
      break;
    }
    case __NR_close: {
      callback(EventInfo{Event::Close, closingFile});
      break;
    }
    default: {
      break;
    }
    }
  }

  withinSyscall = !withinSyscall;
  return true;
}

bool Tracer::waitForSyscall() {
  int sig = 0;
  while (1) {
    if (ptrace(PTRACE_SYSCALL, pid, 0, sig) < 0) {
      LOGPE("ptrace (SYSCALL)");
      return false;
    }
    int status;
  again:
    if (pid_t res = waitpid(pid, &status, 0); res == (pid_t)-1) {
      if (errno == EINTR) {
        if (terminate) {
          LOGI("Termination requested.");
          return false;
        }
        goto again;
      }
      LOGPE("waitpid");
      return false;
    }
    // (WSTOPSIG(status) & 0x80) != 0 in syscall traps
    // if PTRACE_O_TRACESYSGOOD option used
    if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80)
      return true;
    if (WIFEXITED(status)) {
      LOGE("Tracee exited.");
      return false;
    }
    sig = (WIFSTOPPED(status) && WSTOPSIG(status) == SIGCHLD) ? SIGCHLD : 0;
  }
}

bool Tracer::loop() {
  if (!(spawned || attached))
    return false;
  while (iteration())
    ;
  return terminate;
}

pid_t Tracer::traceePid() const { return pid; }

std::string Tracer::traceeCmdLine() const { return cmdLine; }
