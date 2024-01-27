#pragma once

#include "column.hpp"
#include "event.hpp"
#include <atomic>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

class Output {
public:
  Output();
  virtual ~Output();
  void setSorting(Column column);
  void toggleSortingOrder();
  void setProcessInfo(pid_t pid, const std::string &cmd);
  void handleEvent(const EventInfo &event);

protected:
  static constexpr size_t headerHeight{3};
  void update();
  size_t count() const;
  virtual std::ostream &stream() = 0;
  virtual void clear() = 0;
  virtual size_t maxWidth() = 0;
  virtual std::pair<size_t, size_t> linesRange() = 0;

private:
  struct Entry {
    std::string path;
    size_t writeSize{0};
    size_t readSize{0};
    size_t writeCount{0};
    size_t readCount{0};
    size_t openCount{0};
    size_t closeCount{0};
    std::tm lastAccess{};
  };
  static constexpr size_t indexColWidth{4};
  static constexpr size_t sizeColWidth{7};
  static constexpr size_t timeColWidth{12};
  size_t pathColWidth{0};
  std::vector<Entry> list;
  std::atomic<Column> sorting{Column::Path};
  std::atomic_bool reverseSorting{false};
  mutable std::mutex mtx;
  pid_t pid{0};
  std::string cmd;
  void sort();
  void printEntry(size_t index, const Entry &entry);
  void printProcessInfo();
  void printColumnHeaders();
  std::tm now() const;
  std::string formatPath(const std::string &path) const;
  std::string formatSize(size_t size) const;
};

class FileOutput : public Output {
public:
  FileOutput(const char *path);

protected:
  virtual std::ostream &stream() override;
  virtual void clear() override;
  virtual size_t maxWidth() override;
  virtual std::pair<size_t, size_t> linesRange() override;

private:
  std::ofstream file;
};

class TerminalOutput : public Output {
public:
  TerminalOutput();
  void lineUp();
  void lineDown();

protected:
  virtual std::ostream &stream() override;
  virtual void clear() override;
  virtual size_t maxWidth() override;
  virtual std::pair<size_t, size_t> linesRange() override;

private:
  static size_t nCols;
  static size_t nRows;
  size_t scrollDelta{0};
  void escape(const char *cmd);
  static void updateWindowSize();
  static void sigwinchHandler(int);
};
