// src/fortune.c
#include "fortune.h"
#include "vga.h"
#include "pit.h"

static const char *FORTUNES[] = {
    "The best way to predict the future is to invent it. — Alan Kay",
    "Any sufficiently advanced technology is indistinguishable from magic. — Arthur C. Clarke",
    "Talk is cheap. Show me the code. — Linus Torvalds",
    "First, solve the problem. Then, write the code. — John Johnson",
    "It works on my machine. — Every developer ever",
    "There are only two hard things in CS: cache invalidation and naming things. — Phil Karlton",
    "The kernel is the heart of the OS. Guard it well.",
    "In theory, theory and practice are the same. In practice, they are not.",
    "A bug is never just a mistake. It represents something bigger. — Ellen Ullman",
    "Simplicity is the soul of efficiency. — Austin Freeman",
    "Make it work, make it right, make it fast. — Kent Beck",
    "The computer was born to solve problems that did not exist before. — Bill Gates",
    "Bare metal never lies.",
    "Assembly is poetry. C is prose. Everything else is a novel.",
    "Every OS starts with a bootloader and a dream.",
};

#define FORTUNE_COUNT 15

void cmd_fortune(void)
{
    /* Use PIT ticks as entropy for index selection */
    uint32_t idx = pit_ticks() % FORTUNE_COUNT;
    vga_print("\n  ");
    vga_print_color(FORTUNES[idx], 0x0B);
    vga_print("\n\n");
}
