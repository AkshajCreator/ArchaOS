#include "libc.h"

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/readme.txt";

    int fd = open(path, 0);
    if (fd < 0) {
        printf("cat: %s: No such file\n", path);
        return 1;
    }

    char buf[256];
    int bytes_read;
    while ((bytes_read = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, bytes_read);
    }
    close(fd);
    return 0;
}
