#include <archaos.h>
#include <gui.h>
#include <fcntl.h>
#include <unistd.h>

ARCHAOS_GUI_APP("Day of the Tentacle");
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
    if (file_exists("/games/tentacle/TENTACLE.000") ||
        file_exists("/games/tentacle/tentacle.000") ||
        file_exists("/games/tentacle/TENTACLE.001") ||
        file_exists("/games/tentacle/tentacle.001")) {
        return spawn("/bin/scummvm.elf -p /games/tentacle dott");
    }
    return spawn("/bin/scummvm.elf -p /games/tentacle dottdemo");
}
