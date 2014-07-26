# Binder Full of Elves

A binder for elf files. It takes a piece of assembly and an elf file and executes the assembly at the beginning of the elf file.

## Compiling:

```
make
```

## Usage:

```
./bind <path/to/stub> <path/to/elf> <method: 1-3>
```

## Future work:

There is a proof of concept streaming binder for x86_64 linux ELF files in stream_bind.c
- It needs to be added to the make file (compiling it is simply `gcc stream_bind.c`)
- It needs to be make more general
- It needs to be cleaned up
- It needs to take the path to the shell on the command line
- Usage: cat elf | ./stream_bind > elf.backdoored

The resulting binary is output to standard out.
