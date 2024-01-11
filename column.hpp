#pragma once

#include <optional>
#include <string>
#include <vector>

enum class Column {
  Path,
  WriteSize,
  ReadSize,
  WriteCount,
  ReadCount,
  OpenCount,
  CloseCount,
  LastAccess
};

std::optional<Column> stringToColumn(const std::string &str);
std::string columnToString(Column col);
std::vector<std::string> columnNames();
