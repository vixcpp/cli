// Windows headers define ERROR as an object-like macro.  Keep this simulation
// platform-neutral so the public style header is checked on every platform.
#define ERROR 0
#include <vix/cli/Style.hpp>

int main()
{
  static_assert(vix::cli::style::ERROR_TEXT[0] == '\033');
  return 0;
}
