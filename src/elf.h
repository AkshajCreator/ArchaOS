#ifndef ELF_H
#define ELF_H

#include <stdint.h>
#include <stddef.h>

#define EI_NIDENT 16

/* ELF Identification Indices */
#define EI_MAG0        0
#define EI_MAG1        1
#define EI_MAG2        2
#define EI_MAG3        3
#define EI_CLASS       4
#define EI_DATA        5
#define EI_VERSION     6
#define EI_OSABI       7
#define EI_ABIVERSION  8
#define EI_PAD         9

/* Magic numbers */
#define ELFMAG0        0x7f
#define ELFMAG1        'E'
#define ELFMAG2        'L'
#define ELFMAG3        'F'

/* ELF Classes */
#define ELFCLASSNONE   0
#define ELFCLASS32     1
#define ELFCLASS64     2

/* Data encodings */
#define ELFDATANONE    0
#define ELFDATA2LSB    1  /* Little-endian */
#define ELFDATA2MSB    2  /* Big-endian */

/* Object file types */
#define ET_NONE        0
#define ET_REL         1
#define ET_EXEC        2
#define ET_DYN         3
#define ET_CORE        4

/* Architecture */
#define EM_NONE        0
#define EM_386         3  /* Intel 80386 */

/* Version */
#define EV_CURRENT     1

/* Segment types */
#define PT_NULL        0
#define PT_LOAD        1
#define PT_DYNAMIC     2
#define PT_INTERP      3
#define PT_NOTE        4
#define PT_SHLIB       5
#define PT_PHDR        6

/* Segment attributes / permissions */
#define PF_X           0x1  /* Execute */
#define PF_W           0x2  /* Write */
#define PF_R           0x4  /* Read */

typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) Elf32_Phdr;

/* Loader API */
int  elf_check_header(const Elf32_Ehdr *hdr);
int  elf_load_memory(const uint8_t *data, size_t size, const char *name);
int  elf_load_memory_args(const uint8_t *data, size_t size, const char *name, const char *cmd_line);
int  elf_load_file(const char *path);
int  elf_load_file_args(const char *path, const char *cmd_line);
void resolve_elf_path(const char *input, char *out, size_t out_size);
void elf_init_samples(void);

#endif /* ELF_H */


