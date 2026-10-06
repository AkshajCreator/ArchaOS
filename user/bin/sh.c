#include "libc.h"

static void print_prompt(void)
{
    printf("userland$ ");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("\nArchaOS User Space Shell (Ring 3)\n");
    printf("Type 'help' for available commands.\n\n");

    char line[256];
    int pos = 0;

    print_prompt();

    while (1) {
        int c = getchar();
        if (c < 0) {
            yield();
            continue;
        }

        if (c == '\r' || c == '\n') {
            putchar('\n');
            line[pos] = '\0';

            /* Trim leading whitespace */
            char *cmd = line;
            while (*cmd == ' ' || *cmd == '\t') cmd++;

            /* Trim trailing whitespace */
            int len = (int)strlen(cmd);
            while (len > 0 && (cmd[len - 1] == ' ' || cmd[len - 1] == '\t')) {
                cmd[len - 1] = '\0';
                len--;
            }

            if (len > 0) {
                if (strcmp(cmd, "help") == 0) {
                    printf("User Space Shell Commands:\n");
                    printf("  help         - Display this list of commands\n");
                    printf("  pid          - Display current process ID\n");
                    printf("  clear        - Clear the display screen\n");
                    printf("  spawn <path> - Execute standalone ELF binary\n");
                    printf("  exit         - Terminate the user shell\n");
                } else if (strcmp(cmd, "pid") == 0) {
                    printf("Current PID: %d\n", getpid());
                } else if (strcmp(cmd, "clear") == 0) {
                    printf("\033[2J\033[H");
                } else if (strcmp(cmd, "exit") == 0) {
                    printf("Exiting user shell...\n");
                    exit(0);
                } else if (strncmp(cmd, "spawn ", 6) == 0) {
                    const char *target = cmd + 6;
                    while (*target == ' ') target++;
                    if (*target == '\0') {
                        printf("Usage: spawn <path>\n");
                    } else {
                        int child_pid = spawn(target);
                        if (child_pid > 0) {
                            printf("Spawned process '%s' (PID %d)\n", target, child_pid);
                        } else {
                            printf("spawn: failed to execute '%s'\n", target);
                        }
                    }
                } else {
                    printf("sh: command not found: %s\n", cmd);
                }
            }

            pos = 0;
            print_prompt();
        } else if (c == '\b' || c == 127) {
            if (pos > 0) {
                pos--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
        } else if (c >= 32 && c < 127) {
            if (pos < (int)sizeof(line) - 1) {
                line[pos++] = (char)c;
                putchar((char)c);
            }
        }
    }

    return 0;
}
