#pragma once

#include "column.hpp"
#include "event.hpp"
#include <atomic>
#include <codecvt>
#include <cstddef>
#include <ctime>
#include <fstream>
#include <iostream>
#include <locale>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

class Output {
public:
  Output(unsigned delay);
  virtual ~Output();
  void setSorting(Column column);
  void toggleSortingOrder();
  void setProcessInfo(pid_t pid, const std::string &cmd);
  void setFilter(const std::string &filter);
  void handleEvent(const EventInfo &event);

protected:
  void update();
  void start();
  void stop();
  size_t count() const;
  size_t headerHeight() const;
  virtual std::wostream &stream() = 0;
  virtual void clear() = 0;
  virtual size_t maxWidth() const = 0;
  virtual std::pair<size_t, size_t> linesRange() const = 0;
  virtual bool visibleColumnNumbers() const = 0;

private:
  struct Entry {
    enum {
      EventMapped = (1 << 0),
      EventUnlinked = (1 << 1),
      EventRenamed = (1 << 2)
    };
    std::wstring path;
    size_t writeSize{0};
    size_t readSize{0};
    size_t writeCount{0};
    size_t readCount{0};
    size_t openCount{0};
    size_t closeCount{0};
    uint8_t specialEvents{0};
    pid_t lastThread{0};
    std::tm lastAccess{};
    bool filtered{false};
  };
  static constexpr size_t idxWidth{4};
  static constexpr size_t fixedHeaderHeight{3};
  static constexpr size_t minPathColWidth{10};
  size_t colWidth[ColumnsCount]{0, 7, 7, 7, 7, 7, 7, 5, 11, 12};
  std::atomic<Column> sorting{ColPath};
  std::atomic_bool reverseSorting{false};
  pid_t pid{0};
  std::wstring cmd;
  std::string filter;
  std::vector<Entry> list, flist;
  mutable std::mutex mtx;
  std::thread thread;
  int eventFd{-1}, timerFd{-1};
  std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
  void threadRoutine();
  void sort();
  void printEntry(size_t index, const Entry &entry);
  void printProcessInfo();
  void printColumnHeaders();
  Entry &getEntry(const std::wstring &path);
  std::tm now() const;
  std::wstring truncString(const std::wstring &str, size_t maxSize,
                           bool left) const;
  std::wstring formatSize(size_t size) const;
  std::wstring formatEvents(uint8_t state) const;
};

class FileOutput : public Output {
public:
  FileOutput(const char *path, unsigned delay);
  virtual ~FileOutput();

protected:
  virtual std::wostream &stream() override;
  virtual void clear() override;
  virtual size_t maxWidth() const override;
  virtual std::pair<size_t, size_t> linesRange() const override;
  virtual bool visibleColumnNumbers() const override;

private:
  std::wofstream file;
};

class TerminalOutput : public Output {
public:
  TerminalOutput(unsigned delay);
  virtual ~TerminalOutput();
  void pageUp();
  void pageDown();

protected:
  virtual std::wostream &stream() override;
  virtual void clear() override;
  virtual size_t maxWidth() const override;
  virtual std::pair<size_t, size_t> linesRange() const override;
  virtual bool visibleColumnNumbers() const override;

private:
  static size_t nCols;
  static size_t nRows;
  size_t scrollDelta{0};
  void escape(const char *cmd);
  static void updateWindowSize();
  static void sigwinchHandler(int);
};
