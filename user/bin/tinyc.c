#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

/*
 * ArchaOS Tiny Language
 *
 * Example:
 *
 * let x = 10;
 * let y = 20;
 *
 * while (x < y) {
 *     print x;
 *     x = x + 1;
 * }
 *
 * if (x == y) {
 *     print 999;
 * } else {
 *     print 0;
 * }
 */

/* ============================================================
 * Lexer
 * ============================================================ */

#define TOK_EOF      0
#define TOK_NUMBER   1
#define TOK_IDENT    2
#define TOK_PLUS     3
#define TOK_MINUS    4
#define TOK_STAR     5
#define TOK_SLASH    6
#define TOK_LPAREN   7
#define TOK_RPAREN   8
#define TOK_LBRACE   9
#define TOK_RBRACE   10
#define TOK_SEMI     11
#define TOK_ASSIGN   12
#define TOK_EQ       13
#define TOK_NE       14
#define TOK_LT       15
#define TOK_LE       16
#define TOK_GT       17
#define TOK_GE       18
#define TOK_IF       19
#define TOK_ELSE     20
#define TOK_WHILE    21
#define TOK_LET      22
#define TOK_PRINT    23

typedef struct {
    int type;
    int value;
    char text[64];
} Token;

static const char *src;
static size_t src_pos;
static Token current;

static int is_space(char c)
{
    return c == ' ' || c == '\t' ||
    c == '\n' || c == '\r';
}

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') ||
    (c >= 'A' && c <= 'Z') ||
    c == '_';
}

static int is_alnum(char c)
{
    return is_alpha(c) || is_digit(c);
}

static void lexer_error(const char *message)
{
    printf("Lexer error: %s\n", message);
    exit(1);
}

static void next_token(void)
{
    while (is_space(src[src_pos]))
        src_pos++;

    char c = src[src_pos];

    if (c == '\0') {
        current.type = TOK_EOF;
        return;
    }

    if (is_digit(c)) {
        int value = 0;

        while (is_digit(src[src_pos])) {
            value = value * 10 +
            (src[src_pos] - '0');
            src_pos++;
        }

        current.type = TOK_NUMBER;
        current.value = value;
        return;
    }

    if (is_alpha(c)) {
        int i = 0;

        while (is_alnum(src[src_pos])) {
            if (i < 63)
                current.text[i++] = src[src_pos];

            src_pos++;
        }

        current.text[i] = '\0';

        if (strcmp(current.text, "if") == 0)
            current.type = TOK_IF;
        else if (strcmp(current.text, "else") == 0)
            current.type = TOK_ELSE;
        else if (strcmp(current.text, "while") == 0)
            current.type = TOK_WHILE;
        else if (strcmp(current.text, "let") == 0)
            current.type = TOK_LET;
        else if (strcmp(current.text, "print") == 0)
            current.type = TOK_PRINT;
        else
            current.type = TOK_IDENT;

        return;
    }

    src_pos++;

    switch (c) {

        case '+':
            current.type = TOK_PLUS;
            return;

        case '-':
            current.type = TOK_MINUS;
            return;

        case '*':
            current.type = TOK_STAR;
            return;

        case '/':
            current.type = TOK_SLASH;
            return;

        case '(':
            current.type = TOK_LPAREN;
            return;

        case ')':
            current.type = TOK_RPAREN;
            return;

        case '{':
            current.type = TOK_LBRACE;
            return;

        case '}':
            current.type = TOK_RBRACE;
            return;

        case ';':
            current.type = TOK_SEMI;
            return;

        case '=':
            if (src[src_pos] == '=') {
                src_pos++;
                current.type = TOK_EQ;
            } else {
                current.type = TOK_ASSIGN;
            }
            return;

        case '!':
            if (src[src_pos] == '=') {
                src_pos++;
                current.type = TOK_NE;
                return;
            }
            lexer_error("unexpected '!'");
            return;

        case '<':
            if (src[src_pos] == '=') {
                src_pos++;
                current.type = TOK_LE;
            } else {
                current.type = TOK_LT;
            }
            return;

        case '>':
            if (src[src_pos] == '=') {
                src_pos++;
                current.type = TOK_GE;
            } else {
                current.type = TOK_GT;
            }
            return;
    }

    lexer_error("unknown character");
}

/* ============================================================
 * AST
 * ============================================================ */

#define NODE_NUMBER      1
#define NODE_VARIABLE    2
#define NODE_BINARY      3
#define NODE_NEGATE      4
#define NODE_ASSIGN      5
#define NODE_LET         6
#define NODE_PRINT       7
#define NODE_BLOCK       8
#define NODE_IF          9
#define NODE_WHILE       10

typedef struct Node Node;

struct Node {
    int type;

    int value;

    char name[64];

    Node *left;
    Node *right;
    Node *third;

    Node **children;
    int child_count;
};

static Node *new_node(int type)
{
    Node *n = (Node *)malloc(sizeof(Node));

    if (n == NULL) {
        printf("Fatal: out of memory\n");
        exit(1);
    }

    memset(n, 0, sizeof(Node));

    n->type = type;

    return n;
}

static void parse_error(const char *message)
{
    printf("Parse error: %s\n", message);
    exit(1);
}

static void expect(int type)
{
    if (current.type != type)
        parse_error("unexpected token");

    next_token();
}

/* ============================================================
 * Expression Parser
 *
 * expression
 *     comparison
 *
 * comparison
 *     addition [ comparison-op addition ]
 *
 * addition
 *     multiplication { (+|-) multiplication }
 *
 * multiplication
 *     unary { (*|/) unary }
 *
 * unary
 *     -unary
 *     primary
 *
 * primary
 *     NUMBER
 *     IDENT
 *     ( expression )
 * ============================================================ */

static Node *parse_expression(void);

static Node *parse_primary(void)
{
    Node *n;

    if (current.type == TOK_NUMBER) {

        n = new_node(NODE_NUMBER);
        n->value = current.value;

        next_token();

        return n;
    }

    if (current.type == TOK_IDENT) {

        n = new_node(NODE_VARIABLE);

        strcpy(n->name, current.text);

        next_token();

        return n;
    }

    if (current.type == TOK_LPAREN) {

        next_token();

        n = parse_expression();

        expect(TOK_RPAREN);

        return n;
    }

    parse_error("expected expression");

    return NULL;
}

static Node *parse_unary(void)
{
    if (current.type == TOK_MINUS) {

        Node *n;

        next_token();

        n = new_node(NODE_NEGATE);
        n->left = parse_unary();

        return n;
    }

    return parse_primary();
}

static Node *parse_multiplication(void)
{
    Node *left = parse_unary();

    while (current.type == TOK_STAR ||
        current.type == TOK_SLASH) {

        int op = current.type;

    next_token();

    Node *right = parse_unary();

    Node *n = new_node(NODE_BINARY);

    n->value = op;
    n->left = left;
    n->right = right;

    left = n;
        }

        return left;
}

static Node *parse_addition(void)
{
    Node *left = parse_multiplication();

    while (current.type == TOK_PLUS ||
        current.type == TOK_MINUS) {

        int op = current.type;

    next_token();

    Node *right = parse_multiplication();

    Node *n = new_node(NODE_BINARY);

    n->value = op;
    n->left = left;
    n->right = right;

    left = n;
        }

        return left;
}

static Node *parse_comparison(void)
{
    Node *left = parse_addition();

    while (current.type == TOK_EQ ||
        current.type == TOK_NE ||
        current.type == TOK_LT ||
        current.type == TOK_LE ||
        current.type == TOK_GT ||
        current.type == TOK_GE) {

        int op = current.type;

    next_token();

    Node *right = parse_addition();

    Node *n = new_node(NODE_BINARY);

    n->value = op;
    n->left = left;
    n->right = right;

    left = n;
        }

        return left;
}

static Node *parse_expression(void)
{
    return parse_comparison();
}

/* ============================================================
 * Statements
 * ============================================================ */

static Node *parse_statement(void);

static Node *parse_block(void)
{
    Node *block = new_node(NODE_BLOCK);

    block->children = NULL;
    block->child_count = 0;

    expect(TOK_LBRACE);

    while (current.type != TOK_RBRACE &&
        current.type != TOK_EOF) {

        Node *statement = parse_statement();

    Node **new_children;

    new_children =
    (Node **)malloc(
        sizeof(Node *) *
        (block->child_count + 1)
    );

    if (new_children == NULL) {
        printf("Fatal: out of memory\n");
        exit(1);
    }

    if (block->children != NULL) {
        memcpy(
            new_children,
            block->children,
            sizeof(Node *) * block->child_count
        );

        free(block->children);
    }

    new_children[block->child_count] =
    statement;

    block->children = new_children;
    block->child_count++;
        }

        expect(TOK_RBRACE);

        return block;
}

static Node *parse_statement(void)
{
    Node *n;

    /*
     * let x = expression;
     */
    if (current.type == TOK_LET) {

        next_token();

        if (current.type != TOK_IDENT)
            parse_error("expected variable name");

        n = new_node(NODE_LET);

        strcpy(n->name, current.text);

        next_token();

        expect(TOK_ASSIGN);

        n->left = parse_expression();

        expect(TOK_SEMI);

        return n;
    }

    /*
     * print expression;
     */
    if (current.type == TOK_PRINT) {

        next_token();

        n = new_node(NODE_PRINT);

        n->left = parse_expression();

        expect(TOK_SEMI);

        return n;
    }

    /*
     * if (expression) { ... }
     */
    if (current.type == TOK_IF) {

        next_token();

        expect(TOK_LPAREN);

        n = new_node(NODE_IF);

        n->left = parse_expression();

        expect(TOK_RPAREN);

        n->right = parse_block();

        if (current.type == TOK_ELSE) {
            next_token();
            n->third = parse_block();
        }

        return n;
    }

    /*
     * while (expression) { ... }
     */
    if (current.type == TOK_WHILE) {

        next_token();

        expect(TOK_LPAREN);

        n = new_node(NODE_WHILE);

        n->left = parse_expression();

        expect(TOK_RPAREN);

        n->right = parse_block();

        return n;
    }

    /*
     * x = expression;
     */
    if (current.type == TOK_IDENT) {

        char name[64];

        strcpy(name, current.text);

        next_token();

        if (current.type != TOK_ASSIGN)
            parse_error("expected '='");

        next_token();

        n = new_node(NODE_ASSIGN);

        strcpy(n->name, name);

        n->left = parse_expression();

        expect(TOK_SEMI);

        return n;
    }

    /*
     * { ... }
     */
    if (current.type == TOK_LBRACE)
        return parse_block();

    parse_error("unknown statement");

    return NULL;
}

static Node *parse_program(void)
{
    Node *program = new_node(NODE_BLOCK);

    program->children = NULL;
    program->child_count = 0;

    while (current.type != TOK_EOF) {

        Node *statement = parse_statement();

        Node **new_children =
        (Node **)malloc(
            sizeof(Node *) *
            (program->child_count + 1)
        );

        if (new_children == NULL) {
            printf("Fatal: out of memory\n");
            exit(1);
        }

        if (program->children != NULL) {
            memcpy(
                new_children,
                program->children,
                sizeof(Node *) *
                program->child_count
            );

            free(program->children);
        }

        new_children[program->child_count] =
        statement;

        program->children = new_children;
        program->child_count++;
    }

    return program;
}

/* ============================================================
 * Runtime
 * ============================================================ */

#define MAX_VARIABLES 256

typedef struct {
    char name[64];
    int value;
} Variable;

static Variable variables[MAX_VARIABLES];
static int variable_count;

static int get_variable(const char *name)
{
    int i;

    for (i = 0; i < variable_count; i++) {
        if (strcmp(variables[i].name, name) == 0)
            return variables[i].value;
    }

    printf("Runtime error: unknown variable '%s'\n",
           name);

    exit(1);

    return 0;
}

static void set_variable(const char *name, int value)
{
    int i;

    for (i = 0; i < variable_count; i++) {

        if (strcmp(variables[i].name, name) == 0) {
            variables[i].value = value;
            return;
        }
    }

    if (variable_count >= MAX_VARIABLES) {
        printf("Runtime error: too many variables\n");
        exit(1);
    }

    strcpy(
        variables[variable_count].name,
        name
    );

    variables[variable_count].value = value;

    variable_count++;
}

static int evaluate(Node *n)
{
    int a;
    int b;

    if (n == NULL)
        return 0;

    switch (n->type) {

        case NODE_NUMBER:
            return n->value;

        case NODE_VARIABLE:
            return get_variable(n->name);

        case NODE_NEGATE:
            return -evaluate(n->left);

        case NODE_BINARY:

            a = evaluate(n->left);
            b = evaluate(n->right);

            switch (n->value) {

                case TOK_PLUS:
                    return a + b;

                case TOK_MINUS:
                    return a - b;

                case TOK_STAR:
                    return a * b;

                case TOK_SLASH:

                    if (b == 0) {
                        printf(
                            "Runtime error: division by zero\n"
                        );
                        exit(1);
                    }

                    return a / b;

                case TOK_EQ:
                    return a == b;

                case TOK_NE:
                    return a != b;

                case TOK_LT:
                    return a < b;

                case TOK_LE:
                    return a <= b;

                case TOK_GT:
                    return a > b;

                case TOK_GE:
                    return a >= b;
            }
    }

    printf("Runtime error: invalid expression\n");
    exit(1);

    return 0;
}

static void execute(Node *n);

static void execute_block(Node *n)
{
    int i;

    for (i = 0; i < n->child_count; i++)
        execute(n->children[i]);
}

static void execute(Node *n)
{
    int condition;

    if (n == NULL)
        return;

    switch (n->type) {

        case NODE_BLOCK:
            execute_block(n);
            return;

        case NODE_LET:
            set_variable(
                n->name,
                evaluate(n->left)
            );
            return;

        case NODE_ASSIGN:
            set_variable(
                n->name,
                evaluate(n->left)
            );
            return;

        case NODE_PRINT:
            printf("%d\n", evaluate(n->left));
            return;

        case NODE_IF:

            condition = evaluate(n->left);

            if (condition)
                execute(n->right);
        else if (n->third != NULL)
            execute(n->third);

        return;

        case NODE_WHILE:

            /*
             * Safety limit so a broken program
             * cannot accidentally hang the shell
             * forever.
             */
            {
                int iterations = 0;

                while (evaluate(n->left)) {

                    execute(n->right);

                    iterations++;

                    if (iterations > 1000000) {
                        printf(
                            "Runtime error: loop limit exceeded\n"
                        );
                        exit(1);
                    }
                }
            }

            return;
    }

    printf("Runtime error: invalid statement\n");
    exit(1);
}

/* ============================================================
 * File Loading
 * ============================================================ */

static char *load_file(const char *path)
{
    int fd;
    int n;
    int capacity = 1024;
    int length = 0;

    char *buffer =
    (char *)malloc(capacity);

    if (buffer == NULL) {
        printf("Error: out of memory\n");
        exit(1);
    }

    fd = open(path, O_RDONLY);

    if (fd < 0) {
        printf("Error: cannot open '%s'\n", path);
        free(buffer);
        exit(1);
    }

    while (1) {

        if (length + 256 >= capacity) {

            int new_capacity =
            capacity * 2;

            char *new_buffer =
            (char *)malloc(new_capacity);

            if (new_buffer == NULL) {
                printf("Error: out of memory\n");
                close(fd);
                free(buffer);
                exit(1);
            }

            memcpy(
                new_buffer,
                buffer,
                length
            );

            free(buffer);

            buffer = new_buffer;
            capacity = new_capacity;
        }

        n = read(
            fd,
            buffer + length,
            256
        );

        if (n < 0) {
            printf("Error: read failed\n");
            close(fd);
            free(buffer);
            exit(1);
        }

        if (n == 0)
            break;

        length += n;
    }

    close(fd);

    buffer[length] = '\0';

    return buffer;
}

/* ============================================================
 * Main
 * ============================================================ */

int main(int argc, char **argv)
{
    char *program;
    Node *ast;

    if (argc < 2) {
        printf("ArchaOS Tiny Language\n");
        printf("Usage: %s <program.tiny>\n", argv[0]);
        return 1;
    }

    program = load_file(argv[1]);

    printf("TinyC: loading %s\n", argv[1]);

    src = program;
    src_pos = 0;

    variable_count = 0;

    next_token();

    ast = parse_program();

    printf("TinyC: executing...\n");

    execute(ast);

    printf("TinyC: finished.\n");

    free(program);

    return 0;
}
