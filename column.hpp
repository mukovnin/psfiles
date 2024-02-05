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
  ColMemoryMapped,
  ColLastThread,
  ColLastAccess,
  ColumnsCount
};

static const std::wstring columnNames[]{
    L"path",   L"wsize",  L"rsize", L"wcount",  L"rcount",
    L"ocount", L"ccount", L"mm",    L"lthread", L"laccess"};
