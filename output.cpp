#include "output.hpp"
#include "column.hpp"
#include "event.hpp"
#include "log.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <linux/limits.h>
#include <mutex>
#include <ostream>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

Output::Output(unsigned delay) {
  eventFd = eventfd(0, 0);
  if (eventFd == -1) {
    LOGPE("eventfd");
    return;
  }
  timerFd = timerfd_create(CLOCK_MONOTONIC, 0);
  if (timerFd == -1) {
    LOGPE("timerfd_create");
    return;
  }
  itimerspec ts{.it_interval = {.tv_sec = delay, .tv_nsec = 0},
                .it_value = {.tv_sec = delay, .tv_nsec = 0}};
  if (timerfd_settime(timerFd, 0, &ts, nullptr) != 0) {
    LOGPE("timerfd_settime");
    close(timerFd);
    close(eventFd);
    timerFd = eventFd = -1;
  }
}

Output::~Output() {
  if (eventFd != -1)
    close(eventFd);
  if (timerFd != -1)
    close(timerFd);
}

void Output::threadRoutine() {
  pollfd pfds[2]{{.fd = timerFd, .events = POLLIN, .revents = 0},
                 {.fd = eventFd, .events = POLLIN, .revents = 0}};
  bool stop{false};
  while (!stop) {
    if (int ret = poll(pfds, 2, -1); ret == -1) {
      if (errno == EINTR)
        continue;
      LOGPE("poll");
      stop = true;
    } else {
      for (size_t i = 0; !stop && i < 2; ++i) {
        int fd = pfds[i].fd;
        short revs = pfds[i].revents;
        if (revs & POLLIN) {
          uint64_t val;
          char *p{reinterpret_cast<char *>(&val)};
          size_t rest{sizeof(val)};
          while (rest) {
            ssize_t ret = read(fd, p, rest);
            if (ret == -1 && errno != EINTR) {
              LOGPE("read");
              stop = true;
              break;
            }
            rest -= ret;
            p += ret;
          }
          if (fd == eventFd)
            stop = true;
          else if (!stop)
            update();
        } else if (revs & POLLERR) {
          LOGE("Received POLLERR event.");
          stop = true;
        } else if (revs) {
          LOGE("Received unexpected poll event: ##.", std::hex, revs);
          stop = true;
        }
      }
    }
  }
}

void Output::start() {
  if (eventFd != -1 && timerFd != -1 && !thread.joinable())
    thread = std::thread(&Output::threadRoutine, this);
}

void Output::stop() {
  if (thread.joinable()) {
    int64_t val{1};
    ssize_t ret = write(eventFd, &val, sizeof(val));
    if (ret == sizeof(val))
      thread.join();
    else if (ret == -1)
      LOGPE("write (eventfd)");
    else
      LOGE("eventfd: partial write");
  }
}

void Output::setSorting(Column column) {
  if (sorting != column) {
    sorting = column;
    update();
  }
}

void Output::toggleSortingOrder() {
  reverseSorting = !reverseSorting;
  update();
}

void Output::setProcessInfo(pid_t pid, const std::wstring &cmd) {
  std::lock_guard lck(mtx);
  this->pid = pid;
  this->cmd = cmd;
}

void Output::handleEvent(const EventInfo &info) {
  std::lock_guard lck(mtx);
  auto it = std::find_if(list.begin(), list.end(),
                         [&](auto &&item) { return item.path == info.path; });
  bool found = it != list.end();
  if (!found)
    list.emplace_back(Entry{info.path});
  auto &item = found ? *it : list.back();
  switch (info.type) {
  case Event::Open:
    ++item.openCount;
    break;
  case Event::Close:
    ++item.closeCount;
    break;
  case Event::Read:
    ++item.readCount;
    item.readSize += info.arg;
    break;
  case Event::Write:
    ++item.writeCount;
    item.writeSize += info.arg;
    break;
  case Event::MMap:
    item.memoryMapped = true;
    break;
  }
  item.lastAccess = now();
}

void Output::update() {
  std::lock_guard lck(mtx);
  clear();
  printProcessInfo();
  auto it = std::max_element(list.cbegin(), list.cend(),
                             [](auto &&first, auto &&second) {
                               return first.path.size() < second.path.size();
                             });
  if (it == list.cend())
    return;
  size_t maxPathWidth = it->path.size();
  size_t otherColsWidth =
      6 * sizeColWidth + mmapColWidth + timeColWidth + indexColWidth;
  if (maxWidth() < otherColsWidth + 10)
    return;
  pathColWidth = std::min(maxPathWidth, maxWidth() - otherColsWidth);
  printColumnHeaders();
  sort();
  auto [begin, end] = linesRange();
  end = std::min(end, list.size());
  for (size_t i = begin; i < end; ++i)
    printEntry(i + 1, list[i]);
}

size_t Output::count() const {
  std::lock_guard lck(mtx);
  return list.size();
}

void Output::sort() {
  auto compare = [this](const Entry &first, const Entry &second) {
    const auto &f = reverseSorting ? second : first;
    const auto &s = reverseSorting ? first : second;
    switch (sorting) {
    case Column::Path:
      return f.path < s.path;
    case Column::WriteSize:
      return f.writeSize < s.writeSize;
    case Column::ReadSize:
      return f.readSize < s.readSize;
    case Column::WriteCount:
      return f.writeCount < s.writeCount;
    case Column::ReadCount:
      return f.readCount < s.readCount;
    case Column::OpenCount:
      return f.openCount < s.openCount;
    case Column::CloseCount:
      return f.closeCount < s.closeCount;
    case Column::MemoryMapped:
      return f.memoryMapped < s.memoryMapped;
    case Column::LastAccess:
      auto ft = f.lastAccess, st = s.lastAccess;
      return timegm(&ft) < timegm(&st);
    }
    return true;
  };
  std::stable_sort(list.begin(), list.end(), compare);
}

void Output::printEntry(size_t index, const Entry &entry) {
  auto &s = stream();
  s << std::left << std::setw(indexColWidth) << index << std::right;
  s << std::setw(pathColWidth) << truncString(entry.path, pathColWidth);
  s << std::setw(sizeColWidth) << formatSize(entry.writeSize);
  s << std::setw(sizeColWidth) << formatSize(entry.readSize);
  s << std::setw(sizeColWidth) << entry.writeCount;
  s << std::setw(sizeColWidth) << entry.readCount;
  s << std::setw(sizeColWidth) << entry.openCount;
  s << std::setw(sizeColWidth) << entry.closeCount;
  s << std::setw(mmapColWidth) << (entry.memoryMapped ? 'y' : 'n');
  char timeString[50];
  std::strftime(timeString, sizeof(timeString), "%X", &entry.lastAccess);
  s << std::setw(timeColWidth) << conv.from_bytes(timeString);
  s << std::endl;
}

void Output::printColumnHeaders() {
  auto &s = stream();
  if (visibleColumnNumbers()) {
    std::wstring ss =
        L"[S]:" + std::to_wstring(static_cast<unsigned>(sorting.load()) + 1) +
        L"," + (reverseSorting ? L"-" : L"+");
    s << ss;
    s << std::setw(indexColWidth + pathColWidth - ss.size()) << L"[1]";
    for (size_t i = 2; i < 8; ++i)
      s << std::setw(sizeColWidth) << (L"[" + std::to_wstring(i) + L"]");
    s << std::setw(mmapColWidth) << L"[8]";
    s << std::setw(timeColWidth) << L"[9]";
    s << std::endl;
  }
  s << std::setw(indexColWidth + pathColWidth)
    << conv.from_bytes(columnToString(Column::Path));
  for (auto col : {Column::WriteSize, Column::ReadSize, Column::WriteCount,
                   Column::ReadCount, Column::OpenCount, Column::CloseCount}) {
    s << std::setw(sizeColWidth) << conv.from_bytes(columnToString(col));
  }
  s << std::setw(mmapColWidth)
    << conv.from_bytes(columnToString(Column::MemoryMapped));
  s << std::setw(timeColWidth)
    << conv.from_bytes(columnToString(Column::LastAccess));
  s << std::endl;
}

void Output::printProcessInfo() {
  stream() << std::setw(20) << L"PID: " << pid << std::endl
           << std::setw(20) << L"Command line: " << cmd << std::endl;
}

std::tm Output::now() const {
  auto time =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  return *std::localtime(&time);
}

std::wstring Output::truncString(const std::wstring &str,
                                 size_t maxSize) const {
  const std::wstring fill{L"..."};
  const size_t fillLen = fill.size();
  const size_t strLen = str.size();
  if (maxSize >= strLen)
    return str;
  if (maxSize <= fillLen)
    return {};
  size_t pos = strLen + fillLen - maxSize;
  return fill + str.substr(pos);
}

std::wstring Output::formatSize(size_t size) const {
  const std::wstring suffixes = L"bKMGT";
  size_t i = 0;
  while (size > 1024 && i < suffixes.size() - 1) {
    size /= 1024;
    ++i;
  }
  return std::to_wstring(size) + suffixes[i];
}

size_t Output::headerHeight() {
  return fixedHeaderHeight + visibleColumnNumbers();
}

FileOutput::FileOutput(const char *path, unsigned delay)
    : Output(delay), file(path) {
  start();
}

FileOutput::~FileOutput() { stop(); }

std::wostream &FileOutput::stream() { return file; }

void FileOutput::clear() { file.seekp(0); }

size_t FileOutput::maxWidth() { return std::numeric_limits<size_t>::max(); }

std::pair<size_t, size_t> FileOutput::linesRange() {
  return {0, std::numeric_limits<size_t>::max()};
}

bool FileOutput::visibleColumnNumbers() { return false; }

size_t TerminalOutput::nCols;
size_t TerminalOutput::nRows;

TerminalOutput::TerminalOutput(unsigned delay) : Output(delay) {
  signal(SIGWINCH, &TerminalOutput::sigwinchHandler);
  updateWindowSize();
  start();
}

TerminalOutput::~TerminalOutput() { stop(); }

void TerminalOutput::sigwinchHandler(int) { updateWindowSize(); }

void TerminalOutput::updateWindowSize() {
  struct winsize ws;
  ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
  nCols = ws.ws_col;
  nRows = ws.ws_row ? ws.ws_row - 1 : 0;
}

void TerminalOutput::pageUp() {
  size_t m = nRows + scrollDelta;
  size_t n = count() + headerHeight();
  if (m < n && nRows > headerHeight()) {
    scrollDelta += std::min(nRows - headerHeight(), n - m);
    update();
  }
}

void TerminalOutput::pageDown() {
  if (nRows > headerHeight()) {
    size_t n = std::min(scrollDelta, nRows - headerHeight());
    if (n) {
      scrollDelta -= n;
      update();
    }
  }
}

std::wostream &TerminalOutput::stream() { return std::wcout; }

void TerminalOutput::clear() {
  escape("H");
  escape("J");
}

size_t TerminalOutput::maxWidth() { return nCols; }

void TerminalOutput::escape(const char *cmd) { stream() << "\033[" << cmd; }

std::pair<size_t, size_t> TerminalOutput::linesRange() {
  return {scrollDelta, scrollDelta + nRows - headerHeight()};
}

bool TerminalOutput::visibleColumnNumbers() { return true; }
