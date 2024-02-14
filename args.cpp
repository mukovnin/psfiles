#include "args.hpp"
#include "column.hpp"
#include "log.hpp"
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <numeric>
#include <sys/types.h>
#include <unistd.h>

ArgsParser::ArgsParser(int argc, char **argv) : exe(argv[0]) {
  if (!(success = parse(argc, argv)))
    printUsage();
}

bool ArgsParser::parse(int argc, char **argv) {
  std::array<option, argsList.size() + 1> longOpts{};
  std::transform(
      argsList.cbegin(), argsList.cend(), longOpts.begin(), [](const Arg &arg) {
        return option{arg.longName, required_argument, 0, arg.shortName};
      });
  std::string shortOpts =
      std::accumulate(argsList.cbegin(), argsList.cend(), std::string(),
                      [](const std::string &acc, const Arg &arg) {
                        return acc + arg.shortName + ':';
                      });
  int opt;
  while ((opt = getopt_long(argc, argv, shortOpts.data(), longOpts.data(),
                            0)) != -1) {
    switch (opt) {
    case 'o': {
      mOutputFile = optarg;
      break;
    }
    case 's': {
      if (std::string s = optarg; !s.empty()) {
        if (s.back() == '-') {
          s.pop_back();
          mReverseSorting = true;
        }
        auto beg = std::cbegin(columnNames), end = std::cend(columnNames);
        if (auto it = std::find(beg, end, s); it != end) {
          mSortType = static_cast<Column>(std::distance(beg, it));
          break;
        }
      }
      LOGE("Unknown column name: #.", optarg);
      return false;
    }
    case 'd': {
      const char *first = optarg, *last = optarg + strlen(optarg);
      auto [ptr, ec] = std::from_chars(first, last, mDelay);
      if (!(ec == std::errc() && ptr == last && mDelay)) {
        LOGE("Invalid --delay option: must be a positive integer.");
        return false;
      }
      break;
    }
    case 'p': {
      const char *first = optarg, *last = optarg + strlen(optarg);
      auto [ptr, ec] = std::from_chars(first, last, mTraceePid);
      if (!(ec == std::errc() && ptr == last && mTraceePid > 0 &&
            mTraceePid != getpid())) {
        LOGE("Invalid --pid option: must be a positive integer not equal "
             "to current pid.");
        return false;
      }
      break;
    }
    case 'c': {
      mTraceeArgs = argv + optind - 1;
      goto final_check;
    }
    default: {
      return false;
    }
    }
  }
final_check:
  if (static_cast<bool>(mTraceePid) == static_cast<bool>(mTraceeArgs)) {
    LOGE(
        "One and only one of --pid and --cmdline options should be specified.");
    return false;
  }
  return true;
}

pid_t ArgsParser::traceePid() const { return mTraceePid; }

char *const *ArgsParser::traceeArgs() const { return mTraceeArgs; }

Column ArgsParser::sortType() const { return mSortType; }

bool ArgsParser::reverseSorting() const { return mReverseSorting; }

unsigned ArgsParser::delay() const { return mDelay; }

const char *ArgsParser::outputFile() const { return mOutputFile; }

ArgsParser::operator bool() const { return success; }

void ArgsParser::printUsage() const {
  auto print = [](const Arg &arg) {
    std::string left = std::string("-") + arg.shortName + ", " + arg.longName +
                       " " + arg.argName;
    std::cout << std::left << std::setw(25) << left << arg.description
              << std::endl;
  };
  std::cout << "Usage:\n" << exe << " [-osd] -p | -c\n";
  std::for_each(argsList.cbegin(), argsList.cend(), print);
  std::cout << "Column names: ";
  std::copy(std::cbegin(columnNames), std::cend(columnNames),
            std::ostream_iterator<const char *>(std::cout, " "));
  std::cout << std::endl;
}
