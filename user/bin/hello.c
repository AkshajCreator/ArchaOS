#include "libc.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("Hello from ArchaOS!\n");


    int pid = getpid();
    printf("[HELLO] Current Process PID: %d\n", pid);

    printf("[HELLO] Allocating dynamic memory via malloc()...\n");
    char *buf = (char *)malloc(128);
    if (!buf) {
        printf("[HELLO] Error: malloc() failed to allocate memory!\n");
        return 1;
    }

    strcpy(buf, "Ring 3 dynamic heap memory is operational and fully verified.");
    printf("[HELLO] Buffer allocated at %p: \"%s\"\n", (void *)buf, buf);

    printf("[HELLO] Freeing allocated buffer...\n");
    free(buf);
    printf("[HELLO] Memory freed successfully.\n");

    printf("[HELLO] Entering sleep(200 ms)...\n");
    sleep(200);
    printf("[HELLO] Awakened from sleep! Terminating cleanly.\n");

    return 0;
}
