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

  Tracer tracer = args.traceeArgs() ? Tracer(args.traceeArgs())
                                    : Tracer(args.traceePid());

  std::unique_ptr<Output> output;
  if (auto file = args.outputFile()) {
    output.reset(new FileOutput(file, tracer.traceePid(),
                                tracer.traceeCmdLine(), args.delay()));
  } else {
    output.reset(new TerminalOutput(tracer.traceePid(), tracer.traceeCmdLine(),
                                    args.delay()));
  }
  output->setFilter(args.filter());
  output->setSorting(args.sortType());
  if (args.reverseSorting())
    output->toggleSortingOrder();

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
      if (auto out = dynamic_cast<TerminalOutput *>(output.get()))
        out->pageUp();
      break;
    case Command::Down:
      if (auto out = dynamic_cast<TerminalOutput *>(output.get()))
        out->pageDown();
      break;
    }
  };

  std::unique_ptr<Input> input;
  if (!args.outputFile())
    input.reset(new Input(inCallback));

  auto outCallback = [&](const EventInfo &ei) { output->handleEvent(ei); };
  tracer.setOutputCallback(outCallback);

  return tracer.loop() ? EXIT_SUCCESS : EXIT_FAILURE;
}
