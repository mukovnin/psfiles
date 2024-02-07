#include "tracer.hpp"
#include "log.hpp"
#include <algorithm>
#include <asm/unistd.h>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <linux/limits.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

sig_atomic_t Tracer::terminate{0};

Tracer::Tracer(pid_t pid, EventCallback cb) : mainPid(pid), callback(cb) {
  if (!setSignalHandler())
    return;
  cmdLine = getCmdLine();
  auto threads = getProcThreads();
  if (threads.empty())
    return;
  for (auto p : threads) {
    if (ptrace(PTRACE_ATTACH, p, nullptr, nullptr) == -1) {
      LOGPE("ptrace (ATTACH)");
      return;
    }
    if (waitpid(p, nullptr, 0) == -1) {
      LOGPE("waitpid");
      return;
    }
    if (ptrace(PTRACE_SETOPTIONS, p, nullptr, options) == -1) {
      LOGPE("ptrace (SETOPTIONS)");
      return;
    }
    if (ptrace(PTRACE_SYSCALL, p, 0, 0) == -1) {
      LOGPE("ptrace (SYSCALL)");
      return;
    }
  }
  attached = true;
  LOGI("Attached to process with PID # [# thread(s)].", mainPid,
       threads.size());
}

Tracer::Tracer(char *const *argv, EventCallback cb) : callback(cb) {
  if (!setSignalHandler())
    return;
  mainPid = fork();
  if (mainPid < 0) {
    LOGPE("fork");
  } else if (mainPid == 0) {
    spawnTracee(argv);
  } else {
    int status{0};
    if (waitpid(mainPid, &status, 0) == -1) {
      LOGPE("waitpid");
      return;
    }
    if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGSTOP) {
      LOGE("Unexpected wait status: #0x#\n", std::hex, status);
      return;
    }
    cmdLine = getCmdLine();
    if (ptrace(PTRACE_SETOPTIONS, mainPid, nullptr, options) != 0) {
      LOGPE("ptrace (SETOPTIONS)");
      return;
    }
    if (ptrace(PTRACE_SYSCALL, mainPid, 0, 0) < 0) {
      LOGPE("ptrace (SYSCALL)");
      return;
    }
    spawned = true;
    LOGI("Forked (PID #).", mainPid);
  }
}

Tracer::~Tracer() {
  if (spawned) {
    kill(mainPid, SIGTERM);
    LOGI("Sent SIGTERM to tracee (PID #).", mainPid);
  }
}

std::set<pid_t> Tracer::getProcThreads() {
  std::set<pid_t> ret;
  std::string s = "/proc/" + std::to_string(mainPid) + "/task";
  for (const auto &dir_entry : std::filesystem::directory_iterator{s}) {
    if (auto s = dir_entry.path().filename().string(); !s.empty()) {
      pid_t p;
      auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), p);
      if (ec == std::errc() && ptr == s.data() + s.size())
        ret.insert(p);
    }
  }
  return ret;
}

std::wstring Tracer::readLink(const std::string &path) {
  std::string out(PATH_MAX, 0);
  if (readlink(path.data(), out.data(), out.size()) == -1) {
    LOGPE("readlink");
    return invalidFd;
  }
  if (size_t len = out.find('\0'); len != std::string::npos)
    out.resize(len);
  return conv.from_bytes(out);
}

std::wstring Tracer::filePath(int fd) {
  if (mainPid <= 0)
    return {};
  if (fd < 0)
    return invalidFd;
  const wchar_t *std[] = {L"*STDIN*", L"*STDOUT*", L"*STDERR*"};
  if (fd <= 2)
    return std[fd];
  std::string linkPath =
      "/proc/" + std::to_string(mainPid) + "/fd/" + std::to_string(fd);
  return readLink(linkPath);
}

std::wstring Tracer::filePath(int dirFd, const std::wstring &relPath) {
  if (relPath.empty() || relPath.front() == '/')
    return relPath;
  std::wstring dir;
  if (dirFd == AT_FDCWD) {
    std::string linkPath = "/proc/" + std::to_string(mainPid) + "/cwd";
    dir = readLink(linkPath);
  } else {
    dir = filePath(dirFd);
  }
  if (dir.empty())
    return relPath;
  if (dir.back() == '/')
    dir.pop_back();
  return dir + L'/' + relPath;
}

std::wstring Tracer::getCmdLine() {
  std::string path = "/proc/" + std::to_string(mainPid) + "/cmdline";
  std::ifstream file(path);
  if (!file)
    return {};
  std::istreambuf_iterator<char> beg{file}, end;
  std::string str;
  std::transform(beg, end, std::back_inserter(str),
                 [](char c) { return c ? c : ' '; });
  return conv.from_bytes(str);
}

std::wstring Tracer::readString(pid_t tid, void *addr) {
  constexpr size_t wsize{sizeof(int64_t)};
  constexpr size_t nwords{PATH_MAX / wsize};
  union {
    int64_t words[nwords];
    char chars[PATH_MAX]{};
  } __attribute__((__packed__)) data;
  int64_t *p = (int64_t *)addr;
  for (size_t i = 0; i < nwords; ++i, ++p) {
    errno = 0;
    data.words[i] = ptrace(PTRACE_PEEKDATA, tid, p, nullptr);
    if (errno) {
      LOGPE("ptrace (PEEKDATA)");
      return {};
    }
    if (memchr(&data.words[i], '\0', wsize))
      break;
  }
  data.chars[sizeof(data.chars) - 1] = '\0';
  return conv.from_bytes(data.chars);
}

void Tracer::signalHandler(int) { terminate = 1; }

bool Tracer::setSignalHandler() {
  struct sigaction act {};
  sigemptyset(&act.sa_mask);
  act.sa_handler = &Tracer::signalHandler;
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
  pid_t tid;
  do {
    int status;
    tid = waitpid(-1, &status, __WALL);
    if (tid == -1) {
      switch (errno) {
      case EINTR: {
        if (terminate)
          LOGI("Termination requested.");
        else
          tid = 0;
        break;
      }
      case ECHILD: {
        LOGW("Tracee exited.");
        spawned = attached = false;
        break;
      }
      default: {
        LOGPE("waitpid");
        break;
      }
      }
    }
    if (tid > 0) {
      if (WIFSTOPPED(status)) {
        int sig = WSTOPSIG(status);
        bool sysTrap = sig == (SIGTRAP | 0x80);
        if (sysTrap && !handleSyscall(tid))
          return false;
        if (ptrace(PTRACE_SYSCALL, tid, 0, (sig & SIGTRAP) ? 0 : sig) == -1) {
          LOGPE("ptrace (SYSCALL)");
          return false;
        }
        if (!sysTrap)
          tid = 0;
      } else {
        tid = 0;
      }
    }
  } while (tid == 0);
  return tid > 0;
}

bool Tracer::handleSyscall(pid_t tid) {
  auto &ws = withinSyscall[tid];
  ws = !ws;
  user_regs_struct regs;
  if (ptrace(PTRACE_GETREGS, tid, 0, &regs) == -1) {
    LOGPE("ptrace (GETREGS)");
    return false;
  }
  // syscall number: %orig_rax
  // args: %rdi, %rsi, %rdx, %r10, %r8, %r9
  // result: %rax
  int64_t sc = regs.orig_rax;
  if (ws) {
    if (sc == __NR_close)
      closingFiles[tid] = filePath(regs.rdi);
  } else if ((int64_t)regs.rax >= 0) {
    switch (sc) {
    case __NR_read:
    case __NR_readv: {
      callback(
          EventInfo{tid, Event::Read, filePath(regs.rdi), (size_t)regs.rax});
      break;
    }
    case __NR_write:
    case __NR_writev: {
      callback(
          EventInfo{tid, Event::Write, filePath(regs.rdi), (size_t)regs.rax});
      break;
    }
    case __NR_creat:
    case __NR_open:
    case __NR_openat:
    case __NR_openat2: {
      callback(EventInfo{tid, Event::Open, filePath(regs.rax)});
      break;
    }
    case __NR_close: {
      if (auto it = closingFiles.find(tid); it != closingFiles.end()) {
        callback(EventInfo{tid, Event::Close, it->second});
        closingFiles.erase(it);
      }
      break;
    }
    case __NR_mmap: {
      int fd = regs.r8;
      int flags = regs.r10;
      if (!(flags & MAP_ANONYMOUS)) {
        callback(EventInfo{tid, Event::Map, filePath(fd)});
      }
      break;
    }
    case __NR_rename:
    case __NR_renameat:
    case __NR_renameat2: {
      std::wstring from, to;
      int dirFrom, dirTo;
      void *pFrom, *pTo;
      if (sc == __NR_rename) {
        dirFrom = dirTo = AT_FDCWD;
        pFrom = (void *)regs.rdi;
        pTo = (void *)regs.rsi;
      } else {
        dirFrom = regs.rdi;
        dirTo = regs.rdx;
        pFrom = (void *)regs.rsi;
        pTo = (void *)regs.r10;
      }
      from = filePath(dirFrom, readString(tid, pFrom));
      to = filePath(dirTo, readString(tid, pTo));
      callback(EventInfo{tid, Event::Rename, from, 0, to});
      break;
    }
    case __NR_unlink:
    case __NR_unlinkat: {
      int dir;
      void *pPath;
      if (sc == __NR_unlink) {
        dir = AT_FDCWD;
        pPath = (void *)regs.rdi;
      } else {
        dir = regs.rdi;
        pPath = (void *)regs.rsi;
      }
      std::wstring path = filePath(dir, readString(tid, pPath));
      callback(EventInfo{tid, Event::Unlink, path});
      break;
    }
    default: {
      break;
    }
    }
  }
  return true;
}

bool Tracer::loop() {
  if (!(spawned || attached))
    return false;
  while (iteration())
    ;
  return terminate;
}

pid_t Tracer::traceePid() const { return mainPid; }

std::wstring Tracer::traceeCmdLine() const { return cmdLine; }
