#include "elf.h"
#include "vmm.h"
#include "task.h"
#include "fs.h"
#include "serial.h"
#include "user_binaries.h"
#include "user_assets.h"
#include "string.h"
#include <stdint.h>
#include <stddef.h>

static void *elf_memset(void *dst, int val, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)val;
    return dst;
}

int elf_check_header(const Elf32_Ehdr *hdr)
{
    if (!hdr) return -1;
    if (hdr->e_ident[EI_MAG0] != ELFMAG0 ||
        hdr->e_ident[EI_MAG1] != ELFMAG1 ||
        hdr->e_ident[EI_MAG2] != ELFMAG2 ||
        hdr->e_ident[EI_MAG3] != ELFMAG3) {
        return -1;
    }
    if (hdr->e_ident[EI_CLASS] != ELFCLASS32) return -2;
    if (hdr->e_ident[EI_DATA] != ELFDATA2LSB) return -3;
    if (hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN) return -4;
    if (hdr->e_machine != EM_386) return -5;
    return 0;
}

static void elf_copy_to_user_space(uint32_t *user_pd, uint32_t vaddr, const uint8_t *src, size_t len)
{
    size_t written = 0;
    while (written < len) {
        uint32_t cur_va = vaddr + written;
        uint32_t page_offset = cur_va & 0xFFF;
        uint32_t bytes_in_page = PAGE_SIZE - page_offset;
        if (bytes_in_page > len - written) {
            bytes_in_page = len - written;
        }

        uint32_t paddr = vmm_get_physical_address(user_pd, cur_va);
        if (!paddr) break;

        /* Physical memory is identity-mapped in kernel */
        uint8_t *dst = (uint8_t *)paddr;
        for (size_t b = 0; b < bytes_in_page; b++) {
            dst[b] = src[written + b];
        }
        written += bytes_in_page;
    }
}

int elf_load_memory_args(const uint8_t *data, size_t size, const char *name, const char *cmd_line)
{
    if (!data || size < sizeof(Elf32_Ehdr)) {
        serial_puts(COM1_BASE, "[ELF] Error: Binary data buffer too small!\n");
        return -1;
    }

    const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)data;
    if (elf_check_header(ehdr) != 0) {
        serial_puts(COM1_BASE, "[ELF] Error: Invalid ELF32 header or unsupported architecture!\n");
        return -2;
    }

    /* 1. Create a dedicated user page directory */
    uint32_t *user_pd = vmm_create_user_pagedir();
    if (!user_pd) {
        serial_puts(COM1_BASE, "[ELF] Error: Failed to allocate user page directory!\n");
        return -3;
    }

    /* 2. Map and copy all PT_LOAD segments */
    const Elf32_Phdr *phdrs = (const Elf32_Phdr *)(data + ehdr->e_phoff);
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const Elf32_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz == 0) continue;

        uint32_t vstart = ph->p_vaddr & ~0xFFF;
        uint32_t vend   = (ph->p_vaddr + ph->p_memsz + 0xFFF) & ~0xFFF;

        /* Allocate frames and map into user address space */
        for (uint32_t va = vstart; va < vend; va += PAGE_SIZE) {
            uint32_t existing = vmm_get_physical_address(user_pd, va);
            if (!existing) {
                uint32_t pframe = pmm_alloc_frame();
                if (!pframe) {
                    serial_puts(COM1_BASE, "[ELF] Error: Out of physical memory loading segment!\n");
                    vmm_destroy_user_pagedir(user_pd);
                    return -4;
                }
                elf_memset((void *)pframe, 0, PAGE_SIZE);
                vmm_map_page(user_pd, va, pframe, PAGE_PRESENT | PAGE_USER | PAGE_WRITE);
            }
        }

        /* Copy segment file data */
        if (ph->p_filesz > 0 && ph->p_offset + ph->p_filesz <= size) {
            elf_copy_to_user_space(user_pd, ph->p_vaddr, data + ph->p_offset, ph->p_filesz);
        }
    }

    /* 3. Allocate 16 KB User Stack below 3 GB (0xBFFFC000 to 0xBFFFF000) */
    for (uint32_t va = 0xBFFFC000; va < 0xBFFFF000; va += PAGE_SIZE) {
        uint32_t pframe = pmm_alloc_frame();
        if (!pframe) {
            serial_puts(COM1_BASE, "[ELF] Error: Failed to allocate frame for user stack!\n");
            vmm_destroy_user_pagedir(user_pd);
            return -5;
        }
        elf_memset((void *)pframe, 0, PAGE_SIZE);
        vmm_map_page(user_pd, va, pframe, PAGE_PRESENT | PAGE_USER | PAGE_WRITE);
    }

    /* 4. Parse arguments and place strings & argv pointers onto user stack */
    #define ELF_MAX_ARGS 16
    char args_buf[ELF_MAX_ARGS][64];
    uint32_t argv_ptrs[ELF_MAX_ARGS + 1];
    int argc = 0;

    if (cmd_line && cmd_line[0]) {
        const char *p = cmd_line;
        while (*p && argc < ELF_MAX_ARGS) {
            while (*p == ' ') p++;
            if (!*p) break;
            int len = 0;
            while (*p && *p != ' ' && len < 63) {
                args_buf[argc][len++] = *p++;
            }
            args_buf[argc][len] = '\0';
            argc++;
        }
    }

    if (argc == 0) {
        int len = 0;
        const char *src = name ? name : "proc";
        while (src[len] && len < 63) {
            args_buf[0][len] = src[len];
            len++;
        }
        args_buf[0][len] = '\0';
        argc = 1;
    }

    /* Store argument strings in top 512 bytes of user stack (0xBFFFF000 - 512) */
    uint32_t str_cursor = 0xBFFFF000 - 512;
    for (int i = 0; i < argc; i++) {
        size_t slen = strlen(args_buf[i]) + 1;
        elf_copy_to_user_space(user_pd, str_cursor, (const uint8_t *)args_buf[i], slen);
        argv_ptrs[i] = str_cursor;
        str_cursor += (slen + 3) & ~3;
    }
    argv_ptrs[argc] = 0; /* NULL terminator */

    /* User stack layout below strings:
       [esp + 0] = argc
       [esp + 4] = argv[0]
       [esp + 8] = argv[1] ...
       [esp + 4*argc] = argv[argc-1]
       [esp + 4*(argc+1)] = NULL
    */
    uint32_t stack_words = 1 + (argc + 1);
    uint32_t user_esp = (0xBFFFF000 - 512 - (stack_words * 4)) & ~0xF;

    uint32_t u_argc = (uint32_t)argc;
    elf_copy_to_user_space(user_pd, user_esp, (const uint8_t *)&u_argc, 4);
    for (int i = 0; i <= argc; i++) {
        elf_copy_to_user_space(user_pd, user_esp + 4 + i * 4, (const uint8_t *)&argv_ptrs[i], 4);
    }

    /* 5. Create Ring 3 User Task */
    task_t *t = task_create_user(name ? name : "elf_proc", ehdr->e_entry, user_esp, user_pd, 3);
    if (!t) {
        serial_puts(COM1_BASE, "[ELF] Error: Failed to create task control block!\n");
        vmm_destroy_user_pagedir(user_pd);
        return -6;
    }

    serial_printf(COM1_BASE, "[ELF] Successfully loaded '%s' (Entry: 0x%x, User ESP: 0x%x, argc=%d) -> PID %u\n",
                  t->name, ehdr->e_entry, user_esp, argc, t->pid);

    return (int)t->pid;
}

int elf_load_memory(const uint8_t *data, size_t size, const char *name)
{
    return elf_load_memory_args(data, size, name, NULL);
}

int elf_load_file_args(const char *path, const char *cmd_line)
{
    if (!path) return -1;
    fs_node_t *node = fs_resolve(path);
    if (!node || !node->data || node->size == 0) {
        serial_printf(COM1_BASE, "[ELF] File not found or empty: %s\n", path);
        return -1;
    }

    return elf_load_memory_args(node->data, node->size, node->name, cmd_line);
}

int elf_load_file(const char *path)
{
    return elf_load_file_args(path, path);
}

void elf_init_samples(void)
{
    /* Ensure /bin directory exists */
    fs_mkdir("/bin");

    /* Ensure /readme.txt exists for cat.elf */
    if (!fs_resolve("/readme.txt")) {
        fs_touch("/readme.txt");
        static const char readme_txt[] =
            "ArchaOS system readme.\n"
            "Default test file for filesystem reading.\n";
        fs_write("/readme.txt", readme_txt, sizeof(readme_txt) - 1);
        serial_puts(COM1_BASE, "[ELF] Created /readme.txt in RAM VFS\n");
    }

    /* Ensure /docs directory and /docs/dev.txt developer guide exist */
    fs_mkdir("/docs");
    if (!fs_resolve("/docs/dev.txt")) {
        fs_touch("/docs/dev.txt");
        static const char dev_txt[] =
            "======================================================\n"
            "               ArchaOS Developer Guide\n"
            "======================================================\n"
            "\n"
            "1. PROGRAM STRUCTURE\n"
            "Write standard C code with main(argc, argv):\n"
            "    #include <stdio.h>\n"
            "    #include <stdlib.h>\n"
            "    #include <string.h>\n"
            "\n"
            "    int main(int argc, char **argv) {\n"
            "        printf(\"Hello from ArchaOS!\\n\");\n"
            "        return 0;\n"
            "    }\n"
            "\n"
            "2. STANDARD HEADERS (user/include/)\n"
            "    <stdio.h>   : printf, puts, putchar, getchar\n"
            "    <stdlib.h>  : malloc, free, exit, atoi, itoa\n"
            "    <string.h>  : strlen, strcmp, strncmp, strcpy, strncpy, memset, memcpy\n"
            "    <unistd.h>  : read, write, close, sleep, yield, getpid, sbrk\n"
            "    <fcntl.h>   : open, O_RDONLY, O_WRONLY, O_RDWR, O_CREAT\n"
            "    <archaos.h> : All of the above plus process helpers\n"
            "\n"
            "3. COMPILING APPLICATIONS\n"
            "    Method A: Place your .c file in user/bin/ and run 'make'.\n"
            "    Method B: Run './tools/archaos-cc myapp.c -o myapp.elf'.\n"
            "\n"
            "4. RUNNING APPLICATIONS\n"
            "    Simply type the command name in the shell:\n"
            "        myapp\n"
            "    Or with arguments:\n"
            "        myapp arg1 arg2\n"
            "\n"
            "5. GUI WIDGET TOOLKIT (<gui.h>)\n"
            "    int win = gui_create_window(\"Title\", w, h);\n"
            "    gui_add_label(win, x, y, \"Label\");\n"
            "    gui_add_button(win, x, y, w, h, \"Button\");\n"
            "    gui_add_textbox(win, x, y, w, h, \"Default\");\n"
            "    gui_add_checkbox(win, x, y, \"Enable\", 1);\n"
            "    gui_add_progressbar(win, x, y, w, h, 50);\n"
            "    while (gui_poll_event(win, &ev)) { ... }\n"
            "    gui_close_window(win);\n";
        fs_write("/docs/dev.txt", dev_txt, sizeof(dev_txt) - 1);
        serial_puts(COM1_BASE, "[ELF] Created /docs/dev.txt in RAM VFS\n");
    }

    /* Sample script for tinyc interpreter */
    if (!fs_resolve("/test.tiny")) {
        fs_touch("/test.tiny");
        static const char test_tiny[] =
            "let x = 1;\n"
            "let sum = 0;\n"
            "while (x <= 5) {\n"
            "    sum = sum + x;\n"
            "    x = x + 1;\n"
            "}\n"
            "print sum;\n";
        fs_write("/test.tiny", test_tiny, sizeof(test_tiny) - 1);
        serial_puts(COM1_BASE, "[ELF] Created /test.tiny in RAM VFS\n");
    }

    /* Sample BMP image for Image Viewer verification */
    if (!fs_resolve("/sample.bmp")) {
        fs_touch("/sample.bmp");
        #include "sample_bmp.inc"
        fs_write("/sample.bmp", (const char *)sample_bmp_data, sizeof(sample_bmp_data));
        serial_puts(COM1_BASE, "[ELF] Created /sample.bmp in RAM VFS\n");
    }

    /* Sample interdependent C source files for testing multi-file engine */
    if (!fs_resolve("/math_lib.c")) {
        fs_touch("/math_lib.c");
        static const char math_lib_c[] =
            "// ArchaOS Math Library Component\n"
            "int square(int n) {\n"
            "    return n * n;\n"
            "}\n"
            "int add(int a, int b) {\n"
            "    return a + b;\n"
            "}\n";
        fs_write("/math_lib.c", math_lib_c, sizeof(math_lib_c) - 1);
        serial_puts(COM1_BASE, "[ELF] Created /math_lib.c in RAM VFS\n");
    }

    if (!fs_resolve("/main_calc.c")) {
        fs_touch("/main_calc.c");
        static const char main_calc_c[] =
            "// ArchaOS Multi-file Dependent C App\n"
            "#include \"math_lib.c\"\n"
            "\n"
            "int main() {\n"
            "    printf(\"=== Multi-file C Engine Test ===\\n\");\n"
            "    int s = square(7);\n"
            "    int total = add(s, 20);\n"
            "    printf(\"square(7) = %d\\n\", s);\n"
            "    printf(\"total (49 + 20) = %d\\n\", total);\n"
            "    puts(\"Multi-file dependencies verified successfully!\");\n"
            "}\n";
        fs_write("/main_calc.c", main_calc_c, sizeof(main_calc_c) - 1);
        serial_puts(COM1_BASE, "[ELF] Created /main_calc.c in RAM VFS\n");
    }

    /* Automatically register all embedded ELF binaries into RAM VFS */


    for (size_t i = 0; i < EMBEDDED_BINARIES_COUNT; i++) {
        if (fs_mount_const(embedded_binaries[i].path, embedded_binaries[i].data, embedded_binaries[i].size) == 0) {
            serial_printf(COM1_BASE, "[ELF] Registered %s in RAM VFS\n", embedded_binaries[i].path);
        }
    }

    /* Automatically register all embedded user assets into RAM VFS */
#if EMBEDDED_ASSETS_COUNT > 0
    for (size_t i = 0; i < (size_t)EMBEDDED_ASSETS_COUNT; i++) {
        if (!embedded_assets[i].path) continue;
        if (fs_mount_const(embedded_assets[i].path, embedded_assets[i].data, embedded_assets[i].size) == 0) {
            serial_printf(COM1_BASE, "[ASSET] Registered %s (%u bytes) in RAM VFS\n",
                          embedded_assets[i].path, (uint32_t)embedded_assets[i].size);
        }
    }
#endif
}

void resolve_elf_path(const char *input, char *out, size_t out_size)
{
    if (!input || !out || out_size < 8) return;

    /* 1. Direct path check (must be a regular file, not a directory) */
    fs_node_t *node = fs_resolve(input);
    if (node && node->type == FS_FILE) {
        strncpy(out, input, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }

    /* 2. Direct path + .elf */
    char tmp[64];
    strncpy(tmp, input, sizeof(tmp) - 5);
    tmp[sizeof(tmp) - 5] = '\0';
    strcat(tmp, ".elf");
    node = fs_resolve(tmp);
    if (node && node->type == FS_FILE) {
        strncpy(out, tmp, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }

    /* 3. /bin/<input> */
    if (input[0] != '/') {
        strcpy(tmp, "/bin/");
        strncat(tmp, input, 32);
        node = fs_resolve(tmp);
        if (node && node->type == FS_FILE) {
            strncpy(out, tmp, out_size - 1);
            out[out_size - 1] = '\0';
            return;
        }

        /* 4. /bin/<input>.elf */
        strcat(tmp, ".elf");
        node = fs_resolve(tmp);
        if (node && node->type == FS_FILE) {
            strncpy(out, tmp, out_size - 1);
            out[out_size - 1] = '\0';
            return;
        }
    }

    strncpy(out, input, out_size - 1);
    out[out_size - 1] = '\0';
}

