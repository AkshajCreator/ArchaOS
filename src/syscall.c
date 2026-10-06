#include "syscall.h"
#include "vmm.h"
#include "task.h"
#include "fs.h"
#include "vga.h"
#include "serial.h"
#include "pit.h"
#include "gdt.h"
#include "keyboard.h"
#include "gui.h"
#include "string.h"
#include "mm.h"
#include "audio.h"
#include <stdint.h>
#include <stddef.h>

extern void _isr128(void);
extern void idt_set_gate_dpl(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
#include "elf.h"

/* Simple File Descriptor Table per process / global */
#define MAX_SYS_FDS 16
typedef struct {
    int in_use;
    fs_node_t *node;
    uint32_t offset;
} sys_file_t;

static sys_file_t sys_fds[MAX_SYS_FDS];

void syscall_init(void)
{
    for (int i = 0; i < MAX_SYS_FDS; i++) {
        sys_fds[i].in_use = 0;
        sys_fds[i].node = NULL;
        sys_fds[i].offset = 0;
    }
    serial_puts(COM1_BASE, "[SYSCALL] POSIX Syscall subsystem ready (int 0x80 / Vector 128, DPL 3)\n");
}

void syscall_handler(registers_t *regs)
{
    task_t *cur = task_get_current();
    uint32_t syscall_num = regs->eax;
    uint32_t arg1 = regs->ebx;
    uint32_t arg2 = regs->ecx;
    uint32_t arg3 = regs->edx;
    uint32_t arg4 = regs->esi;
    uint32_t arg5 = regs->edi;

    (void)arg4;
    (void)arg5;

    switch (syscall_num) {
        case SYS_EXIT: {
            /* status in arg1 */
            int status = (int)arg1;
            serial_printf(COM1_BASE, "[SYSCALL] sys_exit(%d) called by PID %u ('%s')\n",
                          status, cur ? cur->pid : 0, cur ? cur->name : "unknown");
            if (cur && cur->pid > 0) {
                cur->exit_code = status;
                task_kill(cur->pid);
                task_yield();
            }
            regs->eax = 0;
            break;
        }

        case SYS_SPAWN: {
            const char *cmd = (const char *)arg1;
            if (!cmd) {
                regs->eax = (uint32_t)-1;
                break;
            }
            char path[128];
            size_t i = 0;
            while (cmd[i] && cmd[i] != ' ' && i < sizeof(path) - 1) {
                path[i] = cmd[i];
                i++;
            }
            path[i] = '\0';
            serial_printf(COM1_BASE, "[SYSCALL] sys_spawn('%s') from PID %u\n", cmd, cur ? cur->pid : 0);
            int pid = elf_load_file_args(path, cmd);
            regs->eax = (uint32_t)pid;
            break;
        }

        case SYS_READ: {
            int fd = (int)arg1;
            char *buf = (char *)arg2;
            size_t count = (size_t)arg3;

            if (!buf || count == 0) {
                regs->eax = 0;
                break;
            }

            if (fd == 0) {
                /* STDIN: Read ASCII characters from keyboard FIFO */
                size_t read_bytes = 0;
                while (read_bytes < count && keyboard_has_char()) {
                    buf[read_bytes++] = keyboard_getchar();
                }
                regs->eax = read_bytes;
            } else if (fd >= 3 && fd < MAX_SYS_FDS && sys_fds[fd].in_use) {
                fs_node_t *node = sys_fds[fd].node;
                if (node && node->data) {
                    size_t avail = node->size > sys_fds[fd].offset ? node->size - sys_fds[fd].offset : 0;
                    size_t to_read = count < avail ? count : avail;
                    for (size_t i = 0; i < to_read; i++) {
                        buf[i] = node->data[sys_fds[fd].offset + i];
                    }
                    sys_fds[fd].offset += to_read;
                    regs->eax = to_read;
                } else {
                    regs->eax = 0;
                }
            } else {
                regs->eax = (uint32_t)-1;
            }
            break;
        }

        case SYS_WRITE: {
            int fd = (int)arg1;
            const char *buf = (const char *)arg2;
            size_t count = (size_t)arg3;

            if (!buf || count == 0) {
                regs->eax = 0;
                break;
            }

            if (fd == 1 || fd == 2) {
                /* STDOUT / STDERR */
                for (size_t i = 0; i < count; i++) {
                    vga_print_char(buf[i]);
                    serial_putc(COM1_BASE, buf[i]);
                }
                regs->eax = count;
            } else if (fd >= 3 && fd < MAX_SYS_FDS && sys_fds[fd].in_use) {
                /* Write to file */
                fs_node_t *node = sys_fds[fd].node;
                if (!node || node->type != FS_FILE) {
                    regs->eax = (uint32_t)-1;
                    break;
                }
                uint32_t offset = sys_fds[fd].offset;
                size_t needed = offset + count;

                if (node->is_const || !node->data || needed > node->size) {
                    size_t new_cap = needed < 512 ? 512 : (needed * 2);
                    uint8_t *new_data = kmalloc(new_cap + 1);
                    if (!new_data) {
                        regs->eax = (uint32_t)-1;
                        break;
                    }
                    if (node->data && node->size > 0) {
                        memcpy(new_data, node->data, node->size);
                    }
                    if (node->data && !node->is_const) {
                        kfree(node->data);
                    }
                    node->data = new_data;
                    node->is_const = 0;
                }

                memcpy(node->data + offset, buf, count);
                sys_fds[fd].offset += count;
                if (sys_fds[fd].offset > node->size) {
                    node->size = sys_fds[fd].offset;
                }
                node->data[node->size] = '\0';
                regs->eax = count;
            } else {
                regs->eax = (uint32_t)-1;
            }
            break;
        }

        case SYS_OPEN: {
            const char *path = (const char *)arg1;
            uint32_t flags = arg2;
            if (!path) {
                regs->eax = (uint32_t)-1;
                break;
            }
            fs_node_t *node = fs_resolve(path);
            if (!node) {
                /* Only create file if O_CREAT (0x100) or write flags are specified */
                if ((flags & 0x0100) || (flags & 0x0001) || (flags & 0x0002)) {
                    node = fs_touch(path);
                }
            }
            serial_printf(COM1_BASE, "[SYS_OPEN] '%s' (flags=0x%X) -> node=%p size=%u\n",
                          path, flags, node, node ? (uint32_t)node->size : 0);
            if (!node) {
                regs->eax = (uint32_t)-1;
                break;
            }
            int slot = -1;
            for (int i = 3; i < MAX_SYS_FDS; i++) {
                if (!sys_fds[i].in_use) {
                    slot = i;
                    break;
                }
            }
            if (slot == -1) {
                regs->eax = (uint32_t)-1;
                break;
            }
            sys_fds[slot].in_use = 1;
            sys_fds[slot].node = node;
            sys_fds[slot].offset = 0;
            regs->eax = (uint32_t)slot;
            break;
        }

        case SYS_CLOSE: {
            int fd = (int)arg1;
            if (fd >= 3 && fd < MAX_SYS_FDS && sys_fds[fd].in_use) {
                sys_fds[fd].in_use = 0;
                sys_fds[fd].node = NULL;
                sys_fds[fd].offset = 0;
                regs->eax = 0;
            } else {
                regs->eax = (uint32_t)-1;
            }
            break;
        }

        case 13: /* SYS_LSEEK */ {
            int fd = (int)arg1;
            long offset = (long)arg2;
            int whence = (int)arg3;
            if (fd >= 3 && fd < MAX_SYS_FDS && sys_fds[fd].in_use) {
                fs_node_t *node = sys_fds[fd].node;
                long new_off = (long)sys_fds[fd].offset;
                if (whence == 0) new_off = offset;                          /* SEEK_SET */
                else if (whence == 1) new_off += offset;                   /* SEEK_CUR */
                else if (whence == 2 && node) new_off = (long)node->size + offset; /* SEEK_END */
                if (new_off < 0) new_off = 0;
                if (node && new_off > (long)node->size) new_off = (long)node->size;
                sys_fds[fd].offset = (size_t)new_off;
                regs->eax = (uint32_t)sys_fds[fd].offset;
            } else {
                regs->eax = (uint32_t)-1;
            }
            break;
        }

        case SYS_SLEEP: {
            uint32_t ms = arg1;
            task_sleep(ms);
            regs->eax = 0;
            break;
        }

        case SYS_YIELD: {
            task_yield();
            regs->eax = 0;
            break;
        }

        case SYS_GETPID: {
            regs->eax = cur ? cur->pid : 0;
            break;
        }

        case SYS_MMAP: {
            uint32_t addr = arg1;
            size_t length = (size_t)arg2;

            if (length == 0) {
                regs->eax = 0;
                break;
            }

            if (!cur || !cur->page_dir) {
                regs->eax = 0;
                break;
            }

            /* Allocate page frames and map into user address space */
            uint32_t vaddr = addr ? (addr & ~0xFFF) : (cur->user_brk ? cur->user_brk : 0x20000000);
            size_t pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;

            for (size_t p = 0; p < pages; p++) {
                uint32_t pframe = pmm_alloc_frame();
                if (!pframe) {
                    regs->eax = 0;
                    break;
                }
                vmm_map_page(cur->page_dir, vaddr + p * PAGE_SIZE, pframe, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
            }

            if (cur->user_brk <= vaddr + pages * PAGE_SIZE) {
                cur->user_brk = vaddr + pages * PAGE_SIZE;
            }

            regs->eax = vaddr;
            break;
        }

        case SYS_SBRK: {
            int incr = (int)arg1;
            if (!cur || !cur->page_dir) {
                regs->eax = (uint32_t)-1;
                break;
            }
            if (cur->user_brk == 0) {
                cur->user_brk = 0x20000000;
            }
            uint32_t old_brk = cur->user_brk;
            if (incr > 0) {
                uint32_t new_brk = old_brk + incr;
                uint32_t start_page = old_brk & ~0xFFF;
                uint32_t end_page = (new_brk + PAGE_SIZE - 1) & ~0xFFF;
                for (uint32_t va = start_page; va < end_page; va += PAGE_SIZE) {
                    if (!vmm_get_physical_address(cur->page_dir, va)) {
                        uint32_t pframe = pmm_alloc_frame();
                        if (!pframe) break;
                        uint8_t *pf = (uint8_t *)pframe;
                        for (size_t b = 0; b < PAGE_SIZE; b++) pf[b] = 0;
                        vmm_map_page(cur->page_dir, va, pframe, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
                    }
                }
                cur->user_brk = new_brk;
            }
            regs->eax = old_brk;
            break;
        }

        case SYS_TIME: {
            regs->eax = pit_ticks();
            break;
        }

        case SYS_GUI_CREATE_WINDOW: {
            const char *title = (const char *)regs->ebx;
            int w = (int)regs->ecx;
            int h = (int)regs->edx;
            int win = gui_kernel_create_window(title, w, h);
            serial_printf(COM1_BASE, "[GUI_SYSCALL] CREATE_WINDOW '%s' (%dx%d) by PID %u -> win %d\n",
                          title ? title : "", w, h, cur ? cur->pid : 0, win);
            regs->eax = (uint32_t)win;
            break;
        }

        case SYS_GUI_CLOSE_WINDOW: {
            int win = (int)regs->ebx;
            serial_printf(COM1_BASE, "[GUI_SYSCALL] CLOSE_WINDOW win %d by PID %u\n", win, cur ? cur->pid : 0);
            gui_kernel_close_window(win);
            regs->eax = 0;
            break;
        }

        case SYS_GUI_CLEAR: {
            int win = (int)regs->ebx;
            uint8_t color = (uint8_t)regs->ecx;
            gui_kernel_clear(win, color);
            regs->eax = 0;
            break;
        }

        case SYS_GUI_DRAW_RECT: {
            int win = (int)regs->ebx;
            const gui_draw_rect_args_t *args = (const gui_draw_rect_args_t *)regs->ecx;
            if (args) {
                gui_kernel_draw_rect(win, args->x, args->y, args->w, args->h, args->color);
            }
            regs->eax = 0;
            break;
        }

        case SYS_GUI_DRAW_TEXT: {
            int win = (int)regs->ebx;
            int x = (int)regs->ecx;
            int y = (int)regs->edx;
            const char *text = (const char *)regs->esi;
            uint8_t color = (uint8_t)regs->edi;
            gui_kernel_draw_text(win, x, y, text, color);
            regs->eax = 0;
            break;
        }

        case SYS_GUI_DRAW_PIXEL: {
            int win = (int)regs->ebx;
            int x = (int)regs->ecx;
            int y = (int)regs->edx;
            uint8_t color = (uint8_t)regs->esi;
            gui_kernel_draw_pixel(win, x, y, color);
            regs->eax = 0;
            break;
        }

        case SYS_GUI_UPDATE: {
            int win = (int)regs->ebx;
            gui_kernel_update(win);
            regs->eax = 0;
            break;
        }

        case SYS_GUI_GET_EVENT: {
            int win = (int)regs->ebx;
            gui_raw_event_t *ev_ptr = (gui_raw_event_t *)regs->ecx;
            regs->eax = (uint32_t)gui_kernel_get_event(win, ev_ptr);
            break;
        }

        case SYS_GUI_SET_ICON: {
            int win = (int)regs->ebx;
            const uint8_t *icon_ptr = (const uint8_t *)regs->ecx;
            uint8_t color = (uint8_t)regs->edx;
            gui_kernel_set_icon(win, icon_ptr, color);
            regs->eax = 0;
            break;
        }

        case SYS_GUI_SET_AUTHOR: {
            int win = (int)regs->ebx;
            const char *author_ptr = (const char *)regs->ecx;
            gui_kernel_set_author(win, author_ptr);
            regs->eax = 0;
            break;
        }

        case SYS_GUI_DRAW_BUFFER: {
            int win = (int)regs->ebx;
            const void *buf = (const void *)regs->ecx;
            int w = (int)regs->edx;
            int h = (int)regs->esi;
            int format = (int)regs->edi;
            gui_kernel_draw_buffer(win, buf, w, h, format);
            regs->eax = 0;
            break;
        }

        case SYS_GUI_SET_TITLE: {
            int win = (int)regs->ebx;
            const char *title_ptr = (const char *)regs->ecx;
            gui_kernel_set_title(win, title_ptr);
            regs->eax = 0;
            break;
        }

        case SYS_BEEP: {
            uint32_t freq_hz = (uint32_t)regs->ebx;
            uint32_t ms = (uint32_t)regs->ecx;
            sound_tone(freq_hz, ms);
            regs->eax = 0;
            break;
        }

        case SYS_READDIR: {
            const char *path = (const char *)regs->ebx;
            int index = (int)regs->ecx;
            struct {
                uint32_t d_ino;
                uint8_t  d_type;
                char     d_name[64];
                uint32_t d_size;
            } *out_ent = (void *)regs->edx;

            if (!path || !out_ent) {
                regs->eax = (uint32_t)-1;
                break;
            }

            fs_node_t *dir = fs_resolve(path);
            if (!dir || dir->type != FS_DIR) {
                regs->eax = (uint32_t)-1;
                break;
            }

            if (index < 0 || index >= dir->child_count) {
                regs->eax = 0; /* EOF */
                break;
            }

            fs_node_t *child = dir->children[index];
            if (!child) {
                regs->eax = 0;
                break;
            }

            out_ent->d_ino = (uint32_t)(index + 1);
            out_ent->d_type = (child->type == FS_DIR) ? 2 : 1;
            out_ent->d_size = (uint32_t)child->size;
            strncpy(out_ent->d_name, child->name, sizeof(out_ent->d_name) - 1);
            out_ent->d_name[sizeof(out_ent->d_name) - 1] = '\0';

            regs->eax = 1;
            break;
        }

        case SYS_GUI_SET_CURSOR_MODE: {
            int win = (int)regs->ebx;
            int mode = (int)regs->ecx;
            gui_kernel_set_cursor_mode(win, mode);
            regs->eax = 0;
            break;
        }

        case SYS_AUDIO_WRITE: {
            const void *pcm = (const void *)regs->ebx;
            size_t bytes = (size_t)regs->ecx;
            int channels = (int)regs->edx;
            int sample_rate = (int)regs->esi;
            int bits = (int)regs->edi;

            if (!pcm || bytes == 0) {
                regs->eax = 0;
                break;
            }
            regs->eax = (uint32_t)audio_write_pcm(pcm, bytes, channels, sample_rate, bits);
            break;
        }

        default:
            serial_printf(COM1_BASE, "[SYSCALL] Unknown syscall num %u called by PID %u\n",
                          syscall_num, cur ? cur->pid : 0);
            regs->eax = (uint32_t)-1;
            break;
    }
}
