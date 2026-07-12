#include <ovi/ovi.hpp>

#include <iostream>
#include <string>

int main(int argc, char **argv)
{
  if (argc > 1)
  {
    const std::string arg = argv[1];
    if (arg == "--version")
    {
      std::cout << "ovi " << ovi::version() << "\n";
      return 0;
    }
    if (arg == "--help")
    {
      std::cout << "usage: ovi [--version|--help|greet <name>]\n";
      return 0;
    }
    if (arg == "greet" && argc > 2)
    {
      std::cout << ovi::greet(argv[2]) << "\n";
      return 0;
    }
  }

  std::cout << "ovi ready\n";
  return 0;
}
