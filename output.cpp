#include "output.hpp"
#include "column.hpp"
#include "event.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <linux/limits.h>
#include <ostream>
#include <signal.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

Output::Output() {}

Output::~Output() {}

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

void Output::setProcessInfo(pid_t pid, const std::string &cmd) {
  this->pid = pid;
  this->cmd = cmd;
}

void Output::handleEvent(const EventInfo &info) {
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
  }
  item.lastAccess = now();
  update();
}

void Output::update() {
  clear();
  printProcessInfo();
  auto [begin, end] = linesRange();
  end = std::min(end, list.size());
  auto it = std::max_element(&list[begin], &list[end],
                             [](auto &&first, auto &&second) {
                               return first.path.size() < second.path.size();
                             });
  if (it == &list[end])
    return;
  size_t maxPathWidth = it->path.size();
  ssize_t maxPathColWidth =
      (ssize_t)maxWidth() - 6 * sizeColWidth - timeColWidth - indexColWidth;
  if (maxPathColWidth < 10)
    return;
  pathColWidth = std::min(maxPathWidth, (size_t)maxPathColWidth);
  printColumnHeaders();
  sort();
  for (size_t i = begin; i < end; ++i)
    printEntry(i + 1, list[i]);
}

size_t Output::count() const { return list.size(); }

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
    case Column::LastAccess:
      auto ft = f.lastAccess, st = s.lastAccess;
      return timegm(&ft) < timegm(&st);
    }
    return true;
  };
  std::sort(list.begin(), list.end(), compare);
}

void Output::printEntry(size_t index, const Entry &entry) {
  auto &s = stream();
  s << std::left << std::setw(indexColWidth) << index << std::right;
  s << std::setw(pathColWidth) << formatPath(entry.path);
  s << std::setw(sizeColWidth) << formatSize(entry.writeSize);
  s << std::setw(sizeColWidth) << formatSize(entry.readSize);
  s << std::setw(sizeColWidth) << entry.writeCount;
  s << std::setw(sizeColWidth) << entry.readCount;
  s << std::setw(sizeColWidth) << entry.openCount;
  s << std::setw(sizeColWidth) << entry.closeCount;
  char timeString[50];
  std::strftime(timeString, sizeof(timeString), "%X", &entry.lastAccess);
  s << std::setw(timeColWidth) << timeString;
  s << std::endl;
}

void Output::printColumnHeaders() {
  auto &s = stream();
  s << std::string(indexColWidth, ' ');
  s << std::setw(pathColWidth) << columnToString(Column::Path);
  for (auto col : {Column::WriteSize, Column::ReadSize, Column::WriteCount,
                   Column::ReadCount, Column::OpenCount, Column::CloseCount}) {
    s << std::setw(sizeColWidth) << columnToString(col);
  }
  s << std::setw(timeColWidth) << columnToString(Column::LastAccess);
  s << std::endl;
}

void Output::printProcessInfo() {
  stream() << std::setw(20) << "PID: " << pid << std::endl
           << std::setw(20) << "Command line: " << cmd << std::endl;
}

std::tm Output::now() const {
  auto time =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  return *std::localtime(&time);
}

std::string Output::formatPath(const std::string &path) const {
  const std::string fill{"..."};
  ssize_t strLen = path.size();
  ssize_t delta = (ssize_t)pathColWidth - strLen;
  if (delta >= 0)
    return path;
  size_t pos = strLen - std::max(0L, strLen + delta - (ssize_t)fill.length());
  return fill + path.substr(pos);
}

std::string Output::formatSize(size_t size) const {
  const std::string suffixes = "bKMGT";
  size_t i = 0;
  while (size > 1024 && i < suffixes.size() - 1) {
    size /= 1024;
    ++i;
  }
  return std::to_string(size) + suffixes[i];
}

FileOutput::FileOutput(const char *path) : file(path) {}

std::ostream &FileOutput::stream() { return file; }

void FileOutput::clear() { file.seekp(0); }

size_t FileOutput::maxWidth() { return PATH_MAX + 100; }

std::pair<size_t, size_t> FileOutput::linesRange() { return {0, count()}; }

size_t TerminalOutput::nCols;
size_t TerminalOutput::nRows;

TerminalOutput::TerminalOutput() {
  signal(SIGWINCH, &TerminalOutput::sigwinchHandler);
  updateWindowSize();
}

void TerminalOutput::sigwinchHandler(int) { updateWindowSize(); }

void TerminalOutput::updateWindowSize() {
  struct winsize ws;
  ioctl(STDIN_FILENO, TIOCGWINSZ, &ws);
  nCols = ws.ws_col;
  nRows = ws.ws_row;
}

void TerminalOutput::lineUp() {
  if (nRows + scrollDelta < count() + headerHeight) {
    ++scrollDelta;
    update();
  }
}

void TerminalOutput::lineDown() {
  if (scrollDelta > 0) {
    --scrollDelta;
    update();
  }
}

std::ostream &TerminalOutput::stream() { return std::cout; }

void TerminalOutput::clear() {
  escape("H");
  escape("J");
}

size_t TerminalOutput::maxWidth() { return nCols; }

void TerminalOutput::escape(const char *cmd) { stream() << "\033[" << cmd; }

std::pair<size_t, size_t> TerminalOutput::linesRange() {
  return {scrollDelta, scrollDelta + nRows - headerHeight};
}
