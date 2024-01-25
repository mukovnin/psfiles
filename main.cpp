#include "args.hpp"
#include "event.hpp"
#include "log.hpp"
#include "output.hpp"
#include "tracer.hpp"
#include <cstdlib>
#include <memory>
#include <unistd.h>

int main(int argc, char **argv) {
  ArgsParser args(argc, argv);
  if (!args)
    return EXIT_FAILURE;
  std::unique_ptr<Output> output;
  const char *file = args.outputFile();
  if (file)
    output.reset(new FileOutput(file));
  else
    output.reset(new TerminalOutput);
  output->setSorting(args.sortType());
  if (args.reverseSorting())
    output->toggleSortingOrder();
  auto outCallback = [&](const EventInfo &ei) { output->handleEvent(ei); };
  Tracer tracer = args.traceeArgs() ? Tracer(args.traceeArgs(), outCallback)
                                    : Tracer(args.traceePid(), outCallback);
  output->setProcessInfo(tracer.traceePid(), tracer.traceeCmdLine());
  return tracer.loop() ? EXIT_SUCCESS : EXIT_FAILURE;
}
