#include <archaos.h>
#include <gui.h>
#include <fcntl.h>
#include <unistd.h>

ARCHAOS_GUI_APP("DOOM");
ARCHAOS_APP_AUTHOR("id Software / ArchaOS");

static int file_exists(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        char test_byte = 0;
        int n = read(fd, &test_byte, 1);
        close(fd);
        return (n > 0);
    }
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (file_exists("/games/doom/doom1.wad") ||
        file_exists("/games/doom/DOOM1.WAD") ||
        file_exists("/games/doom/doom.wad") ||
        file_exists("/DOOM/DOOM1.WAD") ||
        file_exists("/DOOM/doom1.wad") ||
        file_exists("/doom1.wad")) {
        return spawn("/bin/doomgeneric.elf");
    }
    return spawn("/bin/doomgeneric.elf");
}
