#include <archaos.h>
#include <gui.h>
#include <fcntl.h>
#include <unistd.h>

ARCHAOS_GUI_APP("Monkey Island");
ARCHAOS_APP_AUTHOR("LucasArts / ArchaOS");

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
    if (file_exists("/games/monkey/MONKEY.000") ||
        file_exists("/games/monkey/monkey.000") ||
        file_exists("/games/monkey/MONKEY1.000") ||
        file_exists("/games/monkey/000.LFL") ||
        file_exists("/games/monkey/DISK01.LEC")) {
        return spawn("/bin/scummvm.elf -p /games/monkey monkey");
    }
    return spawn("/bin/scummvm.elf -p /games/monkey monkeyega");
}
