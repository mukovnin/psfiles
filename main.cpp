#include "args.hpp"
#include "event.hpp"
#include "log.hpp"
#include "tracer.hpp"
#include <cstdlib>
#include <unistd.h>

void callbackStub(const EventInfo &info) {
  LOGI("path # event # arg #", info.path, static_cast<int>(info.type),
       info.arg);
}

int main(int argc, char **argv) {
  ArgsParser args(argc, argv);
  if (!args)
    return EXIT_FAILURE;
  Tracer tracer = args.traceeArgs() ? Tracer(args.traceeArgs(), callbackStub)
                                    : Tracer(args.traceePid(), callbackStub);
  return tracer.loop() ? EXIT_SUCCESS : EXIT_FAILURE;
}
