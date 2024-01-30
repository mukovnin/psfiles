#pragma once

#include "column.hpp"
#include "event.hpp"
#include <atomic>
#include <codecvt>
#include <ctime>
#include <fstream>
#include <iostream>
#include <locale>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class Output {
public:
  Output(unsigned delay);
  virtual ~Output();
  void setSorting(Column column);
  void toggleSortingOrder();
  void setProcessInfo(pid_t pid, const std::wstring &cmd);
  void handleEvent(const EventInfo &event);

protected:
  static constexpr size_t fixedHeaderHeight{3};
  void update();
  size_t count() const;
  void start();
  void stop();
  size_t headerHeight();
  virtual std::wostream &stream() = 0;
  virtual void clear() = 0;
  virtual size_t maxWidth() = 0;
  virtual std::pair<size_t, size_t> linesRange() = 0;
  virtual bool visibleColumnNumbers() = 0;

private:
  struct Entry {
    std::wstring path;
    size_t writeSize{0};
    size_t readSize{0};
    size_t writeCount{0};
    size_t readCount{0};
    size_t openCount{0};
    size_t closeCount{0};
    bool memoryMapped{false};
    std::tm lastAccess{};
  };
  static size_t colWidth[ColumnsCount];
  static constexpr size_t idxWidth{4};
  unsigned interval{1};
  std::vector<Entry> list;
  std::atomic<Column> sorting{ColPath};
  std::atomic_bool reverseSorting{false};
  mutable std::mutex mtx;
  pid_t pid{0};
  std::wstring cmd;
  std::thread thread;
  int eventFd{-1}, timerFd{-1};
  std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
  void threadRoutine();
  void sort();
  void printEntry(size_t index, const Entry &entry);
  void printProcessInfo();
  void printColumnHeaders();
  std::tm now() const;
  std::wstring truncString(const std::wstring &str, size_t maxSize) const;
  std::wstring formatSize(size_t size) const;
};

class FileOutput : public Output {
public:
  FileOutput(const char *path, unsigned delay);
  virtual ~FileOutput();

protected:
  virtual std::wostream &stream() override;
  virtual void clear() override;
  virtual size_t maxWidth() override;
  virtual std::pair<size_t, size_t> linesRange() override;
  virtual bool visibleColumnNumbers() override;

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
  virtual size_t maxWidth() override;
  virtual std::pair<size_t, size_t> linesRange() override;
  virtual bool visibleColumnNumbers() override;

private:
  static size_t nCols;
  static size_t nRows;
  size_t scrollDelta{0};
  void escape(const char *cmd);
  static void updateWindowSize();
  static void sigwinchHandler(int);
};
