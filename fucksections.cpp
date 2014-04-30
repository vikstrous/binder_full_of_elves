#include <fstream>
#include "elfstuff/elfcpp.h"

using namespace std;
using namespace elfcpp;

int main(void) {
  fstream to("hello", fstream::in | fstream::out | fstream::binary);

  to.seekg(0, ifstream::end);
  int to_size = to.tellg();
  to.seekg(0);

  unsigned char to_buff[to_size];
  to.read((char*)to_buff, to_size);

  Ehdr_write<64, false> to_elfw(to_buff);

  to_elfw.put_e_shnum(0);
  to_elfw.put_e_shoff(0);
  to_elfw.put_e_shentsize(0);
  to_elfw.put_e_shstrndx(0);

  to.close();

  // write the results out
  fstream tow("hello", fstream::trunc | fstream::out | fstream::binary);
  tow.seekp(0);
  tow.write((const char*)to_buff, to_size);
  tow.close();

  return 0;
}
