#pragma once

#include "column.hpp"
#include <array>
#include <sys/types.h>

class ArgsParser {
private:
  struct Arg {
    char shortName;
    const char *longName, *argName, *description;
  };
  static constexpr std::array<Arg, 4> argsList{
      {{'o', "output", "FILE", "output to FILE instead of stdout"},
       {'s', "sort", "COLUMN", "sort output by COLUMN"},
       {'p', "pid", "PID", "attach to existing process with id PID"},
       {'c', "cmdline", "CMDLINE", "spawn new process with CMDLINE"}}};
  const char *exe;
  bool success;
  pid_t mTraceePid{0};
  Column mSortType{Column::Path};
  char *const *mTraceeArgs{nullptr};
  const char *mOutputFile{nullptr};
  bool parse(int argc, char **argv);
  void printUsage() const;

public:
  ArgsParser(int argc, char **argv);
  pid_t traceePid() const;
  Column sortType() const;
  char *const *traceeArgs() const;
  const char *outputFile() const;
  operator bool() const;
};
