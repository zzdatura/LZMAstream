#include <iostream>
#include <string_view>
#include "LZMAstream.hpp"

using namespace std;

void save_file(string_view const &filename) {
  oLZMAstream fd{filename};

  fd << "This is interesting"                     << endl
     << "Am I real? Am I going to be compressed?" << endl
     << "I hope I won't die!!!"                   << endl;
}

void read_file(string_view const &filename) {
  iLZMAstream fd{filename};
  for (string line; getline(fd, line);)
    cout << " > " << line << endl;
}

int main() {
  cout << "Open file for writing..." << endl;
  save_file("test.txt.xz");

  cout << endl;

  cout << "Now we open file for reading..." << endl;
  read_file("test.txt.xz");

  return 0;
}
