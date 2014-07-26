#include <fstream>
#include "../elftools/elfcpp.h"

using namespace std;
using namespace elfcpp;

int main(void) {
  fstream to("hello", fstream::in | fstream::out | fstream::binary);

  to.seekg(0, ifstream::end);
  int to_size = to.tellg();
  to.seekg(0);

  unsigned char to_buff[to_size];
  to.read((char*)to_buff, to_size);

  Ehdr<64, false> to_elf(to_buff);
  Ehdr_write<64, false> to_elfw(to_buff);

  int wasted_space = to_elf.get_e_shentsize() * to_elf.get_e_shnum();
  to_elfw.put_e_shnum(0);
  to_elfw.put_e_shoff(0);
  to_elfw.put_e_shentsize(0);
  to_elfw.put_e_shstrndx(0);

  to.close();

  // write the results out
  fstream tow("hello", fstream::trunc | fstream::out | fstream::binary);
  tow.seekp(0);
  tow.write((const char*)to_buff, to_size - wasted_space);
  tow.close();

  return 0;
}
