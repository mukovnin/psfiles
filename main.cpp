#include "args.hpp"
#include <cstdlib>

int main(int argc, char **argv) {
  ArgsParser args(argc, argv);
  if (!args)
    return EXIT_FAILURE;
  return 0;
}
