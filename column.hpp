#pragma once

#include <string>

enum Column {
  ColPath,
  ColWriteSize,
  ColReadSize,
  ColWriteCount,
  ColReadCount,
  ColOpenCount,
  ColCloseCount,
  ColSpecialEvents,
  ColLastThread,
  ColLastAccess,
  ColumnsCount
};

static constexpr const char *columnNames[]{
    "path",   "wsize",  "rsize", "wcount",  "rcount",
    "ocount", "ccount", "spec",  "lthread", "laccess"};
