#include "args.hpp"
#include "tracer.hpp"
#include <cstdlib>
#include <unistd.h>

int main(int argc, char **argv) {
  ArgsParser args(argc, argv);
  if (!args)
    return EXIT_FAILURE;
  Tracer tracer = args.traceeArgs() ? Tracer(args.traceeArgs()) : Tracer(args.traceePid());
  return tracer.loop() ? EXIT_SUCCESS : EXIT_FAILURE;
}
