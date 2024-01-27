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

static constexpr unsigned maxColumnIndex{
    static_cast<unsigned>(Column::LastAccess)};

std::optional<Column> stringToColumn(const std::string &str);
std::string columnToString(Column col);
std::vector<std::string> columnNames();
