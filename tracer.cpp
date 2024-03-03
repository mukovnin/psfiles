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
#include <regex>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
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
  LOGI("Tracer termination reason: #.", strerror(lastErr));
  if (spawned) {
    kill(mainPid, SIGTERM);
    LOGI("Sent SIGTERM to tracee (PID #).", mainPid);
  } else if (attached) {
    auto threads = getProcThreads();
    size_t n{0};
    for (auto p : threads) {
      if (tgkill(mainPid, p, SIGSTOP) == -1) {
        LOGPE("tgkill(SIGSTOP)");
        continue;
      }
      waitpid(p, nullptr, 0);
      if (ptrace(PTRACE_DETACH, p, nullptr, nullptr) == -1) {
        LOGPE("ptrace(DETACH)");
        continue;
      }
      if (tgkill(mainPid, p, SIGCONT) == -1)
        LOGPE("tgkill(SIGCONT)");
      else
        ++n;
    }
    LOGI("Detached from process with PID # [# thread(s)].", mainPid, n);
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

std::string Tracer::readLink(const std::string &path) {
  std::string out(PATH_MAX, 0);
  if (readlink(path.data(), out.data(), out.size()) == -1) {
    LOGPE("readlink");
    return invalidFd;
  }
  if (size_t len = out.find('\0'); len != std::string::npos)
    out.resize(len);
  const std::string deleted = " (deleted)";
  if (out.ends_with(deleted) && !std::filesystem::exists(out))
    out.erase(out.size() - deleted.size());
  return out;
}

std::string Tracer::filePath(int fd) {
  if (fd < 0)
    return invalidFd;
  const char *std[] = {"*STDIN*", "*STDOUT*", "*STDERR*"};
  if (fd <= 2)
    return std[fd];
  std::string linkPath =
      "/proc/" + std::to_string(mainPid) + "/fd/" + std::to_string(fd);
  return readLink(linkPath);
}

std::string Tracer::filePath(int dirFd, const std::string &relPath) {
  if (relPath.empty() || relPath.front() == '/')
    return relPath;
  std::string dir;
  if (dirFd == AT_FDCWD) {
    std::string linkPath = "/proc/" + std::to_string(mainPid) + "/cwd";
    dir = readLink(linkPath);
  } else {
    dir = filePath(dirFd);
  }
  if (dir.empty())
    return relPath;
  return dir + '/' + relPath;
}

std::string Tracer::fixRelativePath(const std::string &path) {
  std::regex current(R"(/\./)");
  std::regex parent(R"(/[^\./]+/\.\./)");
  std::string s(path);
  while (regex_search(s, current))
    s = regex_replace(s, current, "/");
  while (regex_search(s, parent))
    s = regex_replace(s, parent, "/");
  return s;
}

std::string Tracer::getCmdLine() {
  std::string path = "/proc/" + std::to_string(mainPid) + "/cmdline";
  std::ifstream file(path);
  if (!file)
    return {};
  std::istreambuf_iterator<char> beg{file}, end;
  std::string str;
  std::transform(beg, end, std::back_inserter(str),
                 [](char c) { return c ? c : ' '; });
  return str;
}

std::string Tracer::readString(pid_t tid, void *addr) {
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
  return data.chars;
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
    lastErr = errno;
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
        int corrSig = (sig == SIGTRAP || sig == (SIGTRAP | 0x80)) ? 0 : sig;
        if (ptrace(PTRACE_SYSCALL, tid, 0, corrSig) == -1) {
          lastErr = errno;
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
  __ptrace_syscall_info si{};
  constexpr size_t sz{sizeof(__ptrace_syscall_info)};
  if (ptrace(PTRACE_GET_SYSCALL_INFO, tid, sz, &si) == -1) {
    lastErr = errno;
    LOGPE("ptrace (GET_SYSCALL_INFO)");
    return false;
  }
  if (si.op == PTRACE_SYSCALL_INFO_ENTRY) {
    auto &st = state[tid];
    st.nr = si.entry.nr;
    std::copy(std::begin(si.entry.args), std::end(si.entry.args),
              std::begin(st.args));
    if (st.nr == __NR_close)
      closingFiles[tid] = filePath(st.args[0]);
  } else if (si.op == PTRACE_SYSCALL_INFO_EXIT) {
    auto it = state.find(tid);
    if (it == state.end()) {
      LOGE("Unexpected syscall state.");
      return false;
    }
    uint64_t nr = it->second.nr;
    int64_t rval = si.exit.rval;
    uint64_t *args = it->second.args;
    if (rval >= 0) {
      EventInfo ei{};
      switch (nr) {
      case __NR_read:
      case __NR_readv:
      case __NR_preadv:
      case __NR_preadv2:
      case __NR_pread64: {
        ei = {tid, Event::Read, filePath(args[0]), (size_t)rval};
        break;
      }
      case __NR_write:
      case __NR_writev:
      case __NR_pwritev:
      case __NR_pwritev2:
      case __NR_pwrite64: {
        ei = {tid, Event::Write, filePath(args[0]), (size_t)rval};
        break;
      }
      case __NR_creat:
      case __NR_open:
      case __NR_openat:
      case __NR_openat2: {
        ei = {tid, Event::Open, filePath(rval)};
        break;
      }
      case __NR_close: {
        if (auto it = closingFiles.find(tid); it != closingFiles.end()) {
          ei = {tid, Event::Close, it->second};
          closingFiles.erase(it);
        }
        break;
      }
      case __NR_mmap: {
        int fd = args[4];
        int flags = args[3];
        if (!(flags & MAP_ANONYMOUS))
          ei = {tid, Event::Map, filePath(fd)};
        break;
      }
      case __NR_rename:
      case __NR_renameat:
      case __NR_renameat2: {
        std::string from, to;
        int dirFrom, dirTo;
        void *pFrom, *pTo;
        if (nr == __NR_rename) {
          dirFrom = dirTo = AT_FDCWD;
          pFrom = (void *)args[0];
          pTo = (void *)args[1];
        } else {
          dirFrom = args[0];
          dirTo = args[2];
          pFrom = (void *)args[1];
          pTo = (void *)args[3];
        }
        from = filePath(dirFrom, readString(tid, pFrom));
        to = filePath(dirTo, readString(tid, pTo));
        ei = {tid, Event::Rename, from, 0, to};
        break;
      }
      case __NR_unlink:
      case __NR_unlinkat: {
        int dir;
        void *pPath;
        if (nr == __NR_unlink) {
          dir = AT_FDCWD;
          pPath = (void *)args[0];
        } else {
          dir = args[0];
          pPath = (void *)args[1];
        }
        std::string path = filePath(dir, readString(tid, pPath));
        ei = {tid, Event::Unlink, path};
        break;
      }
      default: {
        break;
      }
      }
      if (ei.pid) {
        ei.path = fixRelativePath(ei.path);
        ei.strArg = fixRelativePath(ei.strArg);
        callback(ei);
      }
    }
    state.erase(it);
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

std::string Tracer::traceeCmdLine() const { return cmdLine; }
