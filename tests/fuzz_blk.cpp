// libFuzzer harness. Build with clang and -DBLK_FUZZ=ON, then for example:
//   mkdir -p corpus && ./blk_fuzz -max_total_time=60 corpus ../cases
// Don't commit the corpus.
#include "blk.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>

static BlkEnumLoader fuzz_mode_enum("Mode", {{"FAST", 1}, {"SLOW", 2}});

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  Block b, r;
  std::string s1, s2;
  if (load_block_from_string(std::string(reinterpret_cast<const char *>(data), size), b))
  {
    save_block_to_string(s1, b);
    if (load_block_from_string(s1, r))
    {
      save_block_to_string(s2, r);
      assert(s1 == s2);
    }

    Block copy;
    copy.copy(&b);
    copy.add_detalization(b);
  }
  return 0;
}
