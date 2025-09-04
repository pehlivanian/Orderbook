#include <unistd.h>
#include <iostream>

auto main(int argc, char **argv) -> int {

  long cache_line_size = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
  std::cout << "cache line size: " << cache_line_size << " bytes\n";

  return 0;
}
