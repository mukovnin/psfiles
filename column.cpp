#include "column.hpp"
#include <map>
#include <ranges>

static std::map<std::string, Column> s2c{
    {"path", Column::Path},         {"wsize", Column::WriteSize},
    {"rsize", Column::ReadSize},    {"wcount", Column::WriteCount},
    {"rcount", Column::ReadCount},  {"ocount", Column::OpenCount},
    {"ccount", Column::CloseCount}, {"mm", Column::MemoryMapped},
    {"laccess", Column::LastAccess}};

std::optional<Column> stringToColumn(const std::string &str) {
  if (auto it = s2c.find(str); it != s2c.cend())
    return it->second;
  return std::nullopt;
}

std::string columnToString(Column col) {
  static std::map<Column, std::string> c2s;
  if (c2s.empty()) {
    for (const auto &[title, column] : s2c)
      c2s[column] = title;
  }
  auto it = c2s.find(col);
  if (it != c2s.cend())
    return it->second;
  return {};
}

std::vector<std::string> columnNames() {
  static std::vector<std::string> result;
  if (result.empty()) {
    auto v = s2c | std::views::keys;
    result.assign(v.begin(), v.end());
  }
  return result;
}
