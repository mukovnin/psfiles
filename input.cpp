#include "input.hpp"
#include "column.hpp"
#include "log.hpp"
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

Input::Input(InputCallback cb) : cb(cb) {
  if (tcgetattr(STDIN_FILENO, &termOrigConf) != 0) {
    LOGPE("tcgetattr");
    return;
  }
  termConf = termOrigConf;
  termConf.c_lflag &= ~(ICANON | ECHO);
  termConf.c_cc[VMIN] = 0;
  termConf.c_cc[VTIME] = 0;
  if (tcsetattr(0, TCSANOW, &termConf) != 0) {
    LOGPE("tcsetattr");
    return;
  }
  terminalConfigured = true;
  event = eventfd(0, 0);
  if (event == -1) {
    LOGPE("eventfd");
    return;
  }
  thread = std::thread(&Input::routine, this);
}

Input::~Input() {
  if (thread.joinable()) {
    int64_t val{1};
    ssize_t ret = write(event, &val, sizeof(val));
    if (ret != sizeof(val)) {
      if (ret == -1)
        LOGPE("write (eventfd)");
      else
        LOGE("eventfd: partial write");
    } else {
      thread.join();
    }
  }
  if (event != -1)
    close(event);
  if (terminalConfigured && tcsetattr(0, TCSANOW, &termOrigConf) != 0)
    LOGPE("tcsetattr");
}

void Input::routine() {
  constexpr size_t nfds{2};
  pollfd pfds[nfds]{{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0},
                    {.fd = event, .events = POLLIN, .revents = 0}};
  while (!pfds[nfds - 1].revents) {
    if (int ret = poll(pfds, nfds, -1); ret == -1) {
      if (errno == EINTR)
        continue;
      LOGPE("poll");
      break;
    } else {
      auto revs = pfds[0].revents;
      if (revs & POLLIN) {
        char ch{0};
        ssize_t n = read(STDIN_FILENO, &ch, 1);
        if (n == -1) {
          LOGPE("read");
          break;
        } else if (auto opt = charToCommand(ch); opt) {
          auto [cmd, arg] = *opt;
          cb(cmd, arg);
        }
      } else if (revs & POLLERR) {
        LOGE("Received POLLERR event.");
        break;
      } else if (revs) {
        LOGE("Received unexpected poll event: #0x#.", std::hex, revs);
        break;
      }
    }
  }
}

std::optional<std::pair<Command, unsigned>> Input::charToCommand(char ch) {
  switch (std::toupper(ch)) {
  case 'Q':
    return std::make_pair(Command::Quit, 0);
  case 'S':
    return std::make_pair(Command::SortingOrder, 0);
  case 'P':
    return std::make_pair(Command::Up, 0);
  case 'N':
    return std::make_pair(Command::Down, 0);
  default:
    if (ch >= '0' && (unsigned)ch < '0' + ColumnsCount)
      return std::make_pair(Command::SortingColumn, ch - '0');
  }
  return std::nullopt;
}
