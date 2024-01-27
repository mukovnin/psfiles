#pragma once

#include <functional>
#include <optional>
#include <termios.h>
#include <thread>

enum class Command { SortingColumn, SortingOrder, Down, Up, Quit };
using InputCallback = std::function<void(Command, unsigned arg)>;

class Input {
public:
  Input(InputCallback cb);
  ~Input();

private:
  InputCallback cb;
  std::thread thread;
  int event{-1};
  termios termConf, termOrigConf;
  bool terminalConfigured{false};
  void routine();
  std::optional<std::pair<Command, unsigned>> charToCommand(char ch);
};
