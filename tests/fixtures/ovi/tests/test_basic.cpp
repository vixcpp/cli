#include <ovi/ovi.hpp>

int main()
{
  return ovi::greet("Gaspard") == "Hello, Gaspard!" ? 0 : 1;
}
