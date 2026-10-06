#include "libc.h"

int main(int argc, char **argv)
{
    if (argc >= 4) {
        int a = atoi(argv[1]);
        char op = argv[2][0];
        int b = atoi(argv[3]);
        int res = 0;

        if (op == '+') {
            res = a + b;
        } else if (op == '-') {
            res = a - b;
        } else if (op == '*' || op == 'x') {
            res = a * b;
        } else if (op == '/') {
            if (b == 0) {
                printf("calc: error: division by zero\n");
                return 1;
            }
            res = a / b;
        } else if (op == '%') {
            if (b == 0) {
                printf("calc: error: division by zero\n");
                return 1;
            }
            res = a % b;
        } else {
            printf("calc: unknown operator '%c'\n", op);
            return 1;
        }
        printf("%d %c %d = %d\n", a, op, b, res);
        return 0;
    }

    printf("Usage: calc <num1> <+|-|*|/|%%> <num2>\n");
    printf("Example: calc 42 + 13\n");
    return 0;
}

