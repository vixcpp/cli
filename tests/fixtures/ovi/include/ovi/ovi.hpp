#ifndef OVI_OVI_HPP
#define OVI_OVI_HPP

#include <string>

namespace ovi
{
  inline constexpr const char *version()
  {
    return "0.1.0";
  }

  inline std::string greet(const std::string &name)
  {
    return "Hello, " + name + "!";
  }
}

#endif
