#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: %s <text>\n", argv[0]);
        return 1;
    }

    const char *text = argv[1];
    size_t length = strlen(text);

    printf("Original : %s\n", text);
    printf("Reversed : ");

    for (size_t i = length; i > 0; i--) {
        putchar(text[i - 1]);
    }

    putchar('\n');

    return 0;
}
