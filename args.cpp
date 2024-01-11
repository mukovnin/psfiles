#include "args.hpp"
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
      if (auto opt = stringToColumn(optarg)) {
        mSortType = *opt;
        break;
      }
      return false;
    }
    case 'p': {
      const char *first = optarg, *last = optarg + strlen(optarg);
      auto [ptr, ec] = std::from_chars(first, last, mTraceePid);
      if (!(ec == std::errc() && ptr == last && mTraceePid != getpid()))
        return false;
      break;
    }
    case 'c': {
      mTraceeArgs = argv + optind - 1;
      return true;
    }
    default: {
      return false;
    }
    }
  }
  return static_cast<bool>(mTraceePid) != static_cast<bool>(mTraceeArgs);
}

pid_t ArgsParser::traceePid() const { return mTraceePid; }

char *const *ArgsParser::traceeArgs() const { return mTraceeArgs; }

Column ArgsParser::sortType() const { return mSortType; }

const char *ArgsParser::outputFile() const { return mOutputFile; }

ArgsParser::operator bool() const { return success; }

void ArgsParser::printUsage() const {
  auto print = [](const Arg &arg) {
    std::string left = std::string("-") + arg.shortName + ", " + arg.longName +
                       " " + arg.argName;
    std::cout << std::left << std::setw(25) << left << arg.description
              << std::endl;
  };
  std::cout << "Usage:\n" << exe << " [-os] -p | -c\n";
  std::for_each(argsList.cbegin(), argsList.cend(), print);
  auto names = columnNames();
  std::cout << "Column names: ";
  std::copy(names.cbegin(), names.cend(),
            std::ostream_iterator<std::string>(std::cout, " "));
  std::cout << std::endl;
}
