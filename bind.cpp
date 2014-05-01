#include <fstream>
#include <iostream>
#include <cstring>
#include <bitset>
#include <cstdlib>
// assume little endian
#include "elfstuff/elfcpp.h"

using namespace std;
using namespace elfcpp;

int main(int argc, char* argv[]) {

  // Parse arguments

  if (argc < 3 && argc > 4) {
    cerr << "usage: " << argv[0] << " <stub> <elf> [method: 1,2,3]" << endl;
    return 1;
  }
  int method = 1;
  if (argc == 4) {
    method = atoi(argv[3]);
    if (method < 1 || method > 3) {
      cerr << "usage: " << argv[0] << " <stub> <elf> [method: 1,2,3]" << endl;
      return 1;
    }
  }
  fstream stub(argv[1], fstream::in | fstream::binary);
  if (!stub.is_open()) {
    cerr << "failed to open " << argv[1] << " for reading" << endl;
    return 1;
  }
  fstream to(argv[2], fstream::in | fstream::binary);
  if (!to.is_open()) {
    cerr << "failed to open " << argv[2] << " for reading" << endl;
    return 1;
  }

  // Get the file sizes
  stub.seekg(0, ifstream::end);
  int stub_size = stub.tellg();
  stub.seekg(0);
  int stub_space = ((stub_size / 16) + 1) * 16;
  to.seekg(0, ifstream::end);
  int to_size = to.tellg();
  to.seekg(0);
  int extension_size = 0;

  // not sure if this is necessary
  int align_pad =  0; //((to_size / 16) + 1) * 16 - to_size;
  cerr << "align pad: " << align_pad << endl;

  // allocate memory for the files and read them in
  // thy are allocated on the heap because they may be too big for the stack
  unsigned char *stub_buff = new unsigned char[stub_size];
  unsigned char *to_buff = new unsigned char[to_size + stub_space + align_pad];
  unsigned char *extension;
  cerr << "stub size: " << stub_size << endl;
  stub.read((char*)stub_buff, stub_size);
  to.read((char*)to_buff, to_size);

  // assume little endian, 64 bit
  Ehdr<64, false> to_elf(to_buff);
  Ehdr_write<64, false> to_elfw(to_buff);

  // print the starting addresses
  cerr << to_elf.get_e_entry() << endl;

  // look for the program entry containing the entry point
  int main_phdr;
  for (int i = 0; i < to_elf.get_e_phnum(); i++) {
    Phdr<64, false> to_elf_phdr(to_buff + to_elf.get_e_phoff() + to_elf.get_e_phentsize() * i);
    if (to_elf_phdr.get_p_vaddr() < to_elf.get_e_entry() && to_elf.get_e_entry() < to_elf_phdr.get_p_vaddr() + to_elf_phdr.get_p_memsz()) {
      cerr << "found main phdr" << endl;
      main_phdr = i;
      break;
    }
  }

  // XXX: assume section headers are at end of file
  //int sh_size = to_size - to_elf.get_e_shoff();
  //int old_shoff = to_elf.get_e_shoff();
  // XXX: fuck sections!
  //int wasted_space = to_elf.get_e_shentsize() * to_elf.get_e_shnum();
  to_elfw.put_e_shnum(0);
  to_elfw.put_e_shoff(0);
  to_elfw.put_e_shentsize(0);
  to_elfw.put_e_shstrndx(0);

  Phdr<64, false> to_elf_main_phdr(to_buff + to_elf.get_e_phoff() + to_elf.get_e_phentsize() * main_phdr);
  Phdr_write<64, false> to_elf_main_phdrw(to_buff + to_elf.get_e_phoff() + to_elf.get_e_phentsize() * main_phdr);
  switch (method) {
    /*
     * Here we extend the executable phentry to include the entire file, then we add our code to the end of the file and change the entry point
     */
    case 1:
      //XXX: assume it starts at 0x0
      to_elf_main_phdrw.put_p_memsz(to_size + stub_space);
      to_elf_main_phdrw.put_p_filesz(to_size + stub_space);

      // copy the stub to the end
      cerr << to_size + align_pad << endl;
      cerr << to_size + align_pad + stub_size << endl;
      memset(to_buff + to_size, 0, stub_space + align_pad);
      memcpy(to_buff + to_size + align_pad, stub_buff, stub_size);
      // change the starting address to point to the new entry
      // TODO: dynamically set the return address from the stub to go into the old entry point
      //to_elfw.put_e_entry(to_elf_main_phdr.get_p_vaddr() + to_elf_main_phdr.get_p_memsz() - stub_space);
      to_elfw.put_e_entry(to_elf_main_phdr.get_p_vaddr() + to_size + align_pad);
      break;

    /*
     * Here we extend the non-executable LOAD phentry by the size of the stub, overwriting some non-essential stuff, then we add our code in the newly created executable space and change the entry point
     */
    case 2:
      // find the section to extend
      for (int i = 0; i < to_elf.get_e_phnum(); i++) {
        Phdr<64, false> to_elf_phdr(to_buff + to_elf.get_e_phoff() + to_elf.get_e_phentsize() * i);
        Phdr_write<64, false> to_elf_phdrw(to_buff + to_elf.get_e_phoff() + to_elf.get_e_phentsize() * i);
        int main_phdr_end = to_elf_main_phdr.get_p_offset() + to_elf_main_phdr.get_p_filesz();
        if (to_elf_phdr.get_p_offset() >= main_phdr_end) {
          // the dynamic section is within the load section following the main load section (TODO: detect the right section better?)
          // we extend this section and make it executable
          if (to_elf_phdr.get_p_type() == PT_LOAD) {
            cerr << "found phdr after the main phdr... extending section " << i << endl;
            // extend both the file size and the mem size to contain the stub
            to_elf_phdrw.put_p_memsz(to_elf_phdr.get_p_memsz() + stub_size);
            to_elf_phdrw.put_p_filesz(to_elf_phdr.get_p_filesz() + stub_size);
            // make executable
            to_elf_phdrw.put_p_flags(to_elf_phdr.get_p_flags() | PF_X);
            // the extension is used when the difference between memsz and filesz is so big that all the zeros don't fit in the original elf file, so we make the file bigger
            // create and zero out the extension
            if (to_elf_phdr.get_p_offset() + to_elf_phdr.get_p_memsz() > to_size) {
              extension_size = to_elf_phdr.get_p_offset() + to_elf_phdr.get_p_memsz() - to_size;
              cerr << "extension size: " << extension_size << endl;
              extension = new unsigned char[extension_size];
              memset(extension, 0, extension_size - stub_size);
            }
            // zero out the exta space in the file
            memset(to_buff + to_elf_phdr.get_p_offset() + to_elf_phdr.get_p_filesz(), 0, min(to_elf_phdr.get_p_memsz() - to_elf_phdr.get_p_filesz(), to_size - to_elf_phdr.get_p_offset() - to_elf_phdr.get_p_filesz()));
            // make the file size and mem size the same
            to_elf_phdrw.put_p_filesz(to_elf_phdr.get_p_memsz());
            // copy the stub to the end of either the file or the extension
            if (extension_size == 0) {
              memcpy(to_buff + to_elf_phdr.get_p_offset() + to_elf_phdr.get_p_filesz() - stub_size, stub_buff, stub_size);
            } else {
              memcpy(extension + extension_size - stub_size, stub_buff, stub_size);
            }
            // change the starting address to point to the new entry
            // TODO: dynamically set the return address from the stub to go into the old entry point
            to_elfw.put_e_entry(to_elf_phdr.get_p_vaddr() + to_elf_phdr.get_p_memsz() - stub_size);
         }
        }
      }
      break;

    /*
     * Here we extend the executable LOAD phentry by the size of the stub and move everything after it forward. This makes it so that some of the data is now loaded at different addresses than before, so we need to properly relocate the data. The we change the entry point.
     * XXX: This is not working yet. We need to do the relocation properly
     */
    case 3:
      {


      //unsigned char buf[sh_size];
      //memcpy(buf, to_buff + to_elf.get_e_shoff(), sh_size);
      //memcpy(to_buff + to_elf.get_e_shoff() + stub_size, buf, sh_size);
      //to_elfw.put_e_shoff(to_elf.get_e_shoff() + stub_size);

      // extend the size of the main phdr
      int main_phdr_end = to_elf_main_phdr.get_p_offset() + to_elf_main_phdr.get_p_filesz();
      to_elf_main_phdrw.put_p_memsz(to_elf_main_phdr.get_p_memsz() + stub_space);
      cerr << to_elf_main_phdr.get_p_filesz() << endl;
      to_elf_main_phdrw.put_p_filesz(to_elf_main_phdr.get_p_filesz() + stub_space);

      // move phdrs that were after the phdr we extended
      //XXX: The issue is that there are references in the .text section (code) that use addresses from the 0x6xxxxx range, so moving that requires relocating the addresses in the .text, which is not impossible, but requires more code/tools

      // some virtual addresses are about to change. update the dynamic section before we move it
      for (int i = 0; i < to_elf.get_e_phnum(); i++) {
        Phdr<64, false> to_elf_phdr(to_buff + to_elf.get_e_phoff() + to_elf.get_e_phentsize() * i);
        if (to_elf_phdr.get_p_type() == PT_DYNAMIC) {
            //0x0000000000000019 (DT_INIT_ARRAY)         0x600750
            //0x000000000000001a (DT_FINI_ARRAY)         0x600758
            //0x0000000000000003 (DT_PLTGOT)             0x600940
            // get the location of the dynamic table
            Phdr<64, false> to_elf_dyn_phdr(to_buff + to_elf.get_e_phoff() + to_elf.get_e_phentsize() * i);
            for (int i = 0; ; i++) {
              //TODO: find out why I need *2
              Dyn<64, false> dyn(to_buff + to_elf_phdr.get_p_offset() + sizeof(Dyn<64, false>) * i * 2);
              Dyn_write<64, false> dynw(to_buff + to_elf_phdr.get_p_offset() + sizeof(Dyn<64, false>) * i * 2);
              // advance these vaddr pointers
              if (dyn.get_d_tag() == DT_INIT_ARRAY || dyn.get_d_tag() == DT_FINI_ARRAY || dyn.get_d_tag() == DT_PLTGOT) {
                cerr << "move dynamic tag " << dyn.get_d_tag() << endl;
                dynw.put_d_ptr(dyn.get_d_ptr() + stub_space);
              }
              if (dyn.get_d_tag() == DT_NULL) break;
            }
        }

      }
      // now move the dynamic section and other crap around it: .init_array .fini_array .jcr .dynamic .got .got.plt .data .bss
      for (int i = 0; i < to_elf.get_e_phnum(); i++) {
        Phdr<64, false> to_elf_phdr(to_buff + to_elf.get_e_phoff() + to_elf.get_e_phentsize() * i);
        Phdr_write<64, false> to_elf_phdrw(to_buff + to_elf.get_e_phoff() + to_elf.get_e_phentsize() * i);
        if (to_elf_phdr.get_p_offset() >= main_phdr_end) {
          cerr << "found phdr after the main phdr... moving from " << to_elf_phdr.get_p_offset() << " with size " << to_elf_phdr.get_p_filesz() << " (ends at " << to_elf_phdr.get_p_offset() + to_elf_phdr.get_p_filesz()<< ")" << endl;
          // the dynamic section is within the load section following the main load section (TODO: detect the right section better?)
          if (to_elf_phdr.get_p_type() == PT_LOAD) {
            cerr << "move" << endl;
            unsigned char *buf = new unsigned char[to_elf_phdr.get_p_filesz()];
            cerr << "moving from " << to_elf_phdr.get_p_offset() << " with size " << to_elf_phdr.get_p_filesz() << " (ends at " << to_elf_phdr.get_p_offset() + to_elf_phdr.get_p_filesz()<< ")" << endl;
            memcpy(buf, to_buff + to_elf_phdr.get_p_offset(), to_elf_phdr.get_p_filesz());
            memset(to_buff + to_elf_phdr.get_p_offset(), 0, stub_space);
            cerr << "moving to " << to_elf_phdr.get_p_offset() + stub_space << " with size " << to_elf_phdr.get_p_filesz() << " (ends at " << to_elf_phdr.get_p_offset() + stub_space + to_elf_phdr.get_p_filesz()<< ")" << endl;
            memcpy(to_buff + to_elf_phdr.get_p_offset() + stub_space, buf, to_elf_phdr.get_p_filesz());
            delete[] buf;
          }
          to_elf_phdrw.put_p_offset(to_elf_phdr.get_p_offset() + stub_space);
          to_elf_phdrw.put_p_vaddr(to_elf_phdr.get_p_vaddr() + stub_space);
          to_elf_phdrw.put_p_paddr(to_elf_phdr.get_p_paddr() + stub_space);
        }
      }
      }
      break;

    case 4:
    // repurpose an existing phentry so that we can load our code
      break;

    default:
      cerr << "This should never happen." << endl;
      break;
  }

  to.close();
  stub.close();

  // write the results out
  cerr << "to_size " << to_size << endl;
  cout.write((const char*)to_buff, extension_size > 0 ? to_size : (to_size + stub_space + align_pad));
  cout.write((const char*)extension, extension_size);

  delete[] stub_buff;
  delete[] to_buff;

  return 0;
}
