#include "args.hpp"
#include "event.hpp"
#include "input.hpp"
#include "output.hpp"
#include "tracer.hpp"
#include <cstdlib>
#include <locale>
#include <memory>
#include <pthread.h>
#include <unistd.h>

int main(int argc, char **argv) {
  auto locale = std::locale::global(std::locale(""));
  std::unique_ptr<std::locale, void (*)(std::locale *)> loc(
      &locale, [](auto l) { std::locale::global(*l); });

  ArgsParser args(argc, argv);
  if (!args)
    return EXIT_FAILURE;

  pthread_t mainThread = pthread_self();
  std::unique_ptr<Input> input;
  std::unique_ptr<Output> output;

  auto inCallback = [&](Command cmd, unsigned arg) {
    switch (cmd) {
    case Command::Quit:
      pthread_kill(mainThread, SIGTERM);
      break;
    case Command::SortingOrder:
      output->toggleSortingOrder();
      break;
    case Command::SortingColumn:
      output->setSorting(static_cast<Column>(arg));
      break;
    case Command::Up:
      dynamic_cast<TerminalOutput *>(output.get())->pageUp();
      break;
    case Command::Down:
      dynamic_cast<TerminalOutput *>(output.get())->pageDown();
      break;
    }
  };

  auto outCallback = [&](const EventInfo &ei) { output->handleEvent(ei); };

  if (auto file = args.outputFile()) {
    output.reset(new FileOutput(file, args.delay()));
  } else {
    output.reset(new TerminalOutput(args.delay()));
    input.reset(new Input(inCallback));
  }

  output->setFilter(args.filter());
  output->setSorting(args.sortType());
  if (args.reverseSorting())
    output->toggleSortingOrder();

  Tracer tracer = args.traceeArgs() ? Tracer(args.traceeArgs(), outCallback)
                                    : Tracer(args.traceePid(), outCallback);

  output->setProcessInfo(tracer.traceePid(), tracer.traceeCmdLine());

  return tracer.loop() ? EXIT_SUCCESS : EXIT_FAILURE;
}
