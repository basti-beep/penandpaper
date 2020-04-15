#include <iostream>
#include <sstream>
#include <string>

#include "Markdown.h"

int main(int argc, char **argv) {
  std::ostringstream inp;
  char buffer[4096];
  while (!std::cin.eof()) {
    std::cin.read(buffer, 4096);
    ssize_t num_read = std::cin.gcount();
    inp.write(buffer, num_read);
  }
  std::string inp_str = inp.str();
  try {
    Markdown m(inp_str);
    std::cout << m.process();
  } catch (const std::exception &e) {
    std::cerr << "Unable to parse the markdown: " << e.what() << std::endl;
  }
  return 0;
}
