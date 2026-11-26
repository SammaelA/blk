// libFuzzer harness. Build with clang and -DBLK_FUZZ=ON, then for example:
//   mkdir -p corpus && ./blk_fuzz -max_total_time=60 corpus ../cases
// Don't commit the corpus.
#include "blk.h"
#include <stdexcept>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  try
  {
    Block b, r;
    std::string s;
    if (load_block_from_string(std::string((const char *)data, size), b))
    {
      save_block_to_string(s, b);
      load_block_from_string(s, r);
    }
  }
  catch (const std::logic_error &)
  {
    // std::stoi/stod throw on malformed numbers. That's covered by cases/bad_number.blk;
    // it's ignored here so the fuzzer can get past it and find memory errors.
  }
  return 0;
}
