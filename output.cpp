#include "output.hpp"
#include "column.hpp"
#include "log.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fnmatch.h>
#include <iomanip>
#include <iterator>
#include <limits>
#include <linux/limits.h>
#include <numeric>
#include <poll.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

Output::Output(pid_t pid, const std::string &cmd, unsigned delay)
    : pid(pid), cmd(conv.from_bytes(cmd)) {
  nonPathColsWidth = std::accumulate(&colWidth[ColPath + 1],
                                     &colWidth[ColumnsCount], idxWidth);
  if ((eventFd = eventfd(0, 0)) == -1) {
    LOGPE("eventfd");
    return;
  }
  if ((timerFd = timerfd_create(CLOCK_MONOTONIC, 0)) == -1) {
    LOGPE("timerfd_create");
    close(eventFd);
    eventFd = -1;
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
  list.reserve(10000);
}

Output::~Output() {
  if (eventFd != -1)
    close(eventFd);
  if (timerFd != -1)
    close(timerFd);
}

void Output::threadRoutine() {
  constexpr size_t nfds{2};
  pollfd pfds[nfds]{{.fd = timerFd, .events = POLLIN, .revents = 0},
                    {.fd = eventFd, .events = POLLIN, .revents = 0}};
  bool stop{false};
  while (!stop) {
    if (int ret = poll(pfds, nfds, -1); ret == -1) {
      if (errno == EINTR)
        continue;
      LOGPE("poll");
      stop = true;
    } else {
      for (size_t i = 0; !stop && i < nfds; ++i) {
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
          LOGE("Received unexpected poll event: #0x#.", std::hex, revs);
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
  update();
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
    changed = true;
    update();
  }
}

void Output::toggleSortingOrder() {
  reverseSorting = !reverseSorting;
  changed = true;
  update();
}

void Output::setFilter(const std::string &filter) {
  std::lock_guard lck(mtx);
  this->filter = filter;
}

void Output::handleEvent(const EventInfo &info) {
  if (info.path.empty())
    return;
  if (auto c = info.path.front(); c != '/' && c != '*')
    return;
  std::lock_guard lck(mtx);
  auto &item = getEntry(conv.from_bytes(info.path));
  item.filtered = fnmatch(filter.c_str(), info.path.c_str(), 0) == 0;
  item.lastThread = info.pid;
  item.lastAccess = now();
  switch (info.type) {
  case Event::Open: {
    ++item.openCount;
    break;
  }
  case Event::Close: {
    ++item.closeCount;
    break;
  }
  case Event::Read: {
    ++item.readCount;
    item.readSize += info.sizeArg;
    break;
  }
  case Event::Write: {
    ++item.writeCount;
    item.writeSize += info.sizeArg;
    break;
  }
  case Event::Map: {
    item.specialEvents |= Entry::EventMapped;
    break;
  }
  case Event::Rename: {
    item.specialEvents |= Entry::EventRenamed;
    auto src = item;
    auto &dst = getEntry(conv.from_bytes(info.strArg));
    dst.openCount += src.openCount;
    dst.closeCount += src.closeCount;
    dst.readCount += src.readCount;
    dst.writeCount += src.writeCount;
    dst.readSize += src.readSize;
    dst.writeSize += src.writeSize;
    dst.lastThread = src.lastThread;
    dst.lastAccess = src.lastAccess;
    break;
  }
  case Event::Unlink: {
    item.specialEvents |= Entry::EventUnlinked;
    break;
  }
  }
  changed = true;
}

void Output::update() {
  std::lock_guard lck(mtx);
  clear();
  printProcessInfo();
  if (maxWidth() < nonPathColsWidth + minPathColWidth) {
    stream() << "[insufficient width]\n";
    return;
  }
  if (changed) {
    sort();
    filteredCount = 0;
    maxPathWidth = 0;
    for (const auto &e : list) {
      if (e.filtered) {
        ++filteredCount;
        maxPathWidth = std::max(maxPathWidth, e.path.size());
      }
    }
    changed = false;
  }
  if (!maxPathWidth)
    return;
  colWidth[ColPath] = std::min(maxPathWidth, maxWidth() - nonPathColsWidth);
  printColumnHeaders();
  auto [begin, end] = linesRange();
  end = std::min(end, filteredCount);
  for (size_t i = begin; i < end; ++i)
    printEntry(i + 1, list[i]);
}

size_t Output::count() const {
  std::lock_guard lck(mtx);
  return filteredCount;
}

void Output::sort() {
  auto compare = [this](const Entry &first, const Entry &second) {
    if (first.filtered != second.filtered)
      return first.filtered;
    const auto &f = reverseSorting ? second : first;
    const auto &s = reverseSorting ? first : second;
    switch (sorting) {
    case ColPath:
      return f.path < s.path;
    case ColWriteSize:
      return f.writeSize < s.writeSize;
    case ColReadSize:
      return f.readSize < s.readSize;
    case ColWriteCount:
      return f.writeCount < s.writeCount;
    case ColReadCount:
      return f.readCount < s.readCount;
    case ColOpenCount:
      return f.openCount < s.openCount;
    case ColCloseCount:
      return f.closeCount < s.closeCount;
    case ColSpecialEvents:
      return f.specialEvents < s.specialEvents;
    case ColLastThread:
      return f.lastThread < s.lastThread;
    case ColLastAccess: {
      auto ft = f.lastAccess, st = s.lastAccess;
      return timegm(&ft) < timegm(&st);
    }
    default:
      return true;
    }
  };
  std::stable_sort(list.begin(), list.end(), compare);
}

void Output::printEntry(size_t index, const Entry &entry) {
  auto &s = stream();
  s << std::left << std::setw(idxWidth) << index << std::right;
  s << std::setw(colWidth[ColPath])
    << truncString(entry.path, colWidth[ColPath], true);
  s << std::setw(colWidth[ColWriteSize]) << formatSize(entry.writeSize).c_str();
  s << std::setw(colWidth[ColReadSize]) << formatSize(entry.readSize).c_str();
  s << std::setw(colWidth[ColWriteCount]) << entry.writeCount;
  s << std::setw(colWidth[ColReadCount]) << entry.readCount;
  s << std::setw(colWidth[ColOpenCount]) << entry.openCount;
  s << std::setw(colWidth[ColCloseCount]) << entry.closeCount;
  s << std::setw(colWidth[ColSpecialEvents])
    << formatEvents(entry.specialEvents).c_str();
  s << std::setw(colWidth[ColLastThread]) << entry.lastThread;
  char timeString[50];
  std::strftime(timeString, sizeof(timeString), "%X", &entry.lastAccess);
  s << std::setw(colWidth[ColLastAccess]) << conv.from_bytes(timeString);
  s << std::endl;
}

void Output::printColumnHeaders() {
  auto &s = stream();
  if (visibleControlHints()) {
    std::wstring ss = L"[s]:" +
                      std::to_wstring(static_cast<unsigned>(sorting.load())) +
                      (reverseSorting ? L"-" : L"+") + L" [n]↓ [p]↑ [q]";
    s << ss;
    s << std::setw(idxWidth + colWidth[ColPath] - ss.size()) << "[0]";
    for (size_t i = ColPath + 1; i < ColumnsCount; ++i)
      s << std::setw(colWidth[i]) << (L"[" + std::to_wstring(i) + L"]");
    s << std::endl;
  }
  std::wstring sCount = L"(" + std::to_wstring(filteredCount) +
                        (filteredCount == 1 ? L" file" : L" files") + L")";
  s << sCount;
  s << std::setw(idxWidth + colWidth[ColPath] - sCount.size())
    << columnNames[ColPath];
  for (size_t i = ColPath + 1; i < ColumnsCount; ++i)
    s << std::setw(colWidth[i]) << columnNames[i];
  s << std::endl;
}

void Output::printProcessInfo() {
  constexpr size_t left{20};
  if (maxWidth() <= left)
    return;
  stream() << std::setw(left) << "PID: " << pid << std::endl
           << std::setw(left)
           << "Command line: " << truncString(cmd, maxWidth() - left, false)
           << std::endl;
}

std::tm Output::now() const {
  auto time =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  return *std::localtime(&time);
}

Output::Entry &Output::getEntry(const std::wstring &path) {
  auto it = std::find_if(list.begin(), list.end(),
                         [&](auto &&item) { return item.path == path; });
  if (it != list.end())
    return *it;
  list.emplace_back(Entry{path});
  return list.back();
}

std::wstring Output::truncString(const std::wstring &str, size_t maxSize,
                                 bool left) const {
  const std::wstring fill{L"..."};
  const size_t fillLen = fill.size();
  const size_t strLen = str.size();
  if (maxSize >= strLen)
    return str;
  if (maxSize <= fillLen)
    return {};
  if (left) {
    size_t pos = strLen + fillLen - maxSize;
    return fill + str.substr(pos);
  }
  return str.substr(0, maxSize - fillLen) + fill;
}

std::string Output::formatSize(size_t size) const {
  if (size < 1024)
    return std::to_string(size) + "b";
  const std::string suffixes = "KMGT";
  float fsize = size;
  size_t i = 0;
  while ((fsize /= 1024) >= 1000 && i < suffixes.size() - 1)
    ++i;
  char buf[7]{};
  std::snprintf(buf, sizeof(buf), "%4.1f%c", fsize, suffixes[i]);
  return buf;
}

std::string Output::formatEvents(uint8_t events) const {
  std::string s;
  if (events & Entry::EventMapped)
    s += 'm';
  if (events & Entry::EventRenamed)
    s += 'r';
  if (events & Entry::EventUnlinked)
    s += 'u';
  if (s.empty())
    s = '-';
  return s;
}

size_t Output::headerHeight() const {
  return fixedHeaderHeight + visibleControlHints();
}

FileOutput::FileOutput(const char *path, pid_t pid, const std::string &cmd,
                       unsigned delay)
    : Output(pid, cmd, delay), file(path) {
  start();
}

FileOutput::~FileOutput() { stop(); }

std::wostream &FileOutput::stream() { return file; }

void FileOutput::clear() { file.seekp(0); }

size_t FileOutput::maxWidth() const {
  return std::numeric_limits<size_t>::max();
}

std::pair<size_t, size_t> FileOutput::linesRange() const {
  return {0, std::numeric_limits<size_t>::max()};
}

bool FileOutput::visibleControlHints() const { return false; }

size_t TerminalOutput::nCols;
size_t TerminalOutput::nRows;

TerminalOutput::TerminalOutput(pid_t pid, const std::string &cmd,
                               unsigned delay)
    : Output(pid, cmd, delay) {
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

void TerminalOutput::pageDown() {
  size_t m = nRows + scrollDelta;
  size_t n = count() + headerHeight();
  if (m < n && nRows > headerHeight()) {
    scrollDelta += std::min(nRows - headerHeight(), n - m);
    update();
  }
}

void TerminalOutput::pageUp() {
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

size_t TerminalOutput::maxWidth() const { return nCols; }

void TerminalOutput::escape(const char *cmd) { stream() << "\033[" << cmd; }

std::pair<size_t, size_t> TerminalOutput::linesRange() const {
  return {scrollDelta, scrollDelta + nRows - headerHeight()};
}

bool TerminalOutput::visibleControlHints() const { return true; }
