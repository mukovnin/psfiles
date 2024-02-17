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
  static constexpr std::array<Arg, 6> argsList{
      {{'o', "output", "FILE", "output to FILE instead of stdout"},
       {'s', "sort", "COLUMN", "sort output by COLUMN"},
       {'f', "filter", "GLOB", "filter filepaths with GLOB"},
       {'d', "delay", "SECONDS", "interval between list updates"},
       {'p', "pid", "PID", "attach to existing process with id PID"},
       {'c', "cmdline", "CMDLINE", "spawn new process with CMDLINE"}}};
  const char *exe;
  bool success;
  pid_t mTraceePid{0};
  Column mSortType{ColPath};
  bool mReverseSorting{false};
  unsigned mDelay{1};
  char *const *mTraceeArgs{nullptr};
  const char *mOutputFile{nullptr};
  const char *mFilter{"*"};
  bool parse(int argc, char **argv);
  void printUsage() const;

public:
  ArgsParser(int argc, char **argv);
  pid_t traceePid() const;
  Column sortType() const;
  bool reverseSorting() const;
  unsigned delay() const;
  char *const *traceeArgs() const;
  const char *outputFile() const;
  const char *filter() const;
  operator bool() const;
};
