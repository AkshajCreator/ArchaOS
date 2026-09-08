// src/shellext.c
// Shell extensions for ArchaOS:
//   - Output redirection  (> and >>)
//   - Pipe               (|)
//   - Aliases            (alias name=value)
//   - Script runner      (run file)
//   - history, wc, grep  commands

#include "shellext.h"
#include "kernel.h"
#include "vga.h"
#include "fs.h"
#include "serial.h"
#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * STRING HELPERS
 * ============================================================ */

static int se_strlen(const char *s)
{ int n=0; while(s[n]) n++; return n; }

static __attribute__((unused)) void se_strcpy(char *d, const char *s)
{ while((*d++=*s++)); }

static void se_strncpy(char *d, const char *s, int n)
{ int i = 0; while (i < n - 1 && s[i]) { d[i] = s[i]; i++; } d[i] = '\0'; }

/* se_strcat removed as unused */

static int se_strcmp(const char *a, const char *b)
{ while(*a&&*a==*b){a++;b++;} return *(unsigned char*)a-*(unsigned char*)b; }

static int se_strncmp(const char *a, const char *b, int n)
{ while(n&&*a&&*a==*b){a++;b++;n--;} return n?(*(unsigned char*)a-*(unsigned char*)b):0; }

/* se_memset removed as unused */

/* ============================================================
 * CAPTURE BUFFER
 * Used to intercept vga_print output for pipes/redirection
 * ============================================================ */

#define CAP_SIZE 4096
static char    cap_buf[CAP_SIZE];
static int     cap_len   = 0;
static int     capturing = 0;

void shellext_capture_char(char c)
{
    if (!capturing) return;
    if (cap_len < CAP_SIZE - 1) cap_buf[cap_len++] = c;
}

void shellext_capture_start(void)
{
    cap_len   = 0;
    cap_buf[0]= '\0';
    capturing = 1;
}

void shellext_capture_stop(void)
{
    capturing   = 0;
    cap_buf[cap_len] = '\0';
}

int shellext_is_capturing(void)
{
    return capturing;
}

const char *shellext_get_captured(void)
{
    return cap_buf;
}

/* ============================================================
 * ALIAS TABLE
 * ============================================================ */

#define MAX_ALIASES  16
#define ALIAS_NAMELEN 24
#define ALIAS_VALLEN  64

typedef struct { char name[ALIAS_NAMELEN]; char val[ALIAS_VALLEN]; } alias_t;
static alias_t aliases[MAX_ALIASES];
static int     alias_count = 0;

void alias_set(const char *name, const char *value)
{
    /* Update existing */
    for (int i=0;i<alias_count;i++) {
        if (se_strcmp(aliases[i].name, name)==0) {
            int vl=se_strlen(value);
            if(vl>=ALIAS_VALLEN) vl=ALIAS_VALLEN-1;
            int j; for(j=0;j<vl;j++) aliases[i].val[j]=value[j];
            aliases[i].val[j]='\0';
            vga_print("alias updated\n");
            return;
        }
    }
    if (alias_count >= MAX_ALIASES) { vga_print("alias: table full\n"); return; }
    int nl=se_strlen(name); if(nl>=ALIAS_NAMELEN) nl=ALIAS_NAMELEN-1;
    int vl=se_strlen(value); if(vl>=ALIAS_VALLEN) vl=ALIAS_VALLEN-1;
    int i; for(i=0;i<nl;i++) aliases[alias_count].name[i]=name[i];
    aliases[alias_count].name[i]='\0';
    for(i=0;i<vl;i++) aliases[alias_count].val[i]=value[i];
    aliases[alias_count].val[i]='\0';
    alias_count++;
    vga_print("alias set\n");
}

void alias_list(void)
{
    if (!alias_count) { vga_print("No aliases defined.\n"); return; }
    for (int i=0;i<alias_count;i++) {
        vga_print("  "); vga_print(aliases[i].name);
        vga_print(" = "); vga_print(aliases[i].val);
        vga_print("\n");
    }
}

/* Expand alias — returns pointer to expanded string or NULL */
static const char *alias_expand(const char *cmd)
{
    /* Match first word of cmd against alias names */
    for (int i=0;i<alias_count;i++) {
        int nl = se_strlen(aliases[i].name);
        if (se_strncmp(cmd, aliases[i].name, nl)==0 &&
            (cmd[nl]=='\0'||cmd[nl]==' '))
        {
            static char expanded[256];
            int ei = 0;
            const char *val = aliases[i].val;
            while (*val && ei < 255) expanded[ei++] = *val++;
            if (cmd[nl] == ' ') {
                if (ei < 255) expanded[ei++] = ' ';
                const char *rest = cmd + nl + 1;
                while (*rest && ei < 255) expanded[ei++] = *rest++;
            }
            expanded[ei] = '\0';
            return expanded;
        }
    }
    return 0;
}

/* ============================================================
 * ENVIRONMENT VARIABLES
 * ============================================================ */

#define MAX_ENV_VARS  32
#define ENV_NAMELEN   24
#define ENV_VALLEN    64

typedef struct {
    char name[ENV_NAMELEN];
    char val[ENV_VALLEN];
} env_t;

static env_t env_vars[MAX_ENV_VARS];
static int   env_count = 0;
static int   env_initialized = 0;

void env_init(void)
{
    if (env_initialized) return;
    env_initialized = 1;
    env_count = 0;

    env_set("USER", "root");
    env_set("OS", "ArchaOS");
    env_set("SHELL", "vga_shell");
    env_set("TERM", "vga-80x25");
    env_set("ARCH", "i386");
    env_set("HOME", "/");
}

void env_set(const char *name, const char *value)
{
    if (!env_initialized) env_init();

    /* Update existing */
    for (int i = 0; i < env_count; i++) {
        if (se_strcmp(env_vars[i].name, name) == 0) {
            int vl = se_strlen(value);
            if (vl >= ENV_VALLEN) vl = ENV_VALLEN - 1;
            int j; for (j = 0; j < vl; j++) env_vars[i].val[j] = value[j];
            env_vars[i].val[j] = '\0';
            return;
        }
    }
    if (env_count >= MAX_ENV_VARS) { vga_print("export: environment table full\n"); return; }
    int nl = se_strlen(name); if (nl >= ENV_NAMELEN) nl = ENV_NAMELEN - 1;
    int vl = se_strlen(value); if (vl >= ENV_VALLEN) vl = ENV_VALLEN - 1;
    int i; for (i = 0; i < nl; i++) env_vars[env_count].name[i] = name[i];
    env_vars[env_count].name[i] = '\0';
    for (i = 0; i < vl; i++) env_vars[env_count].val[i] = value[i];
    env_vars[env_count].val[i] = '\0';
    env_count++;
}

const char *env_get(const char *name)
{
    if (!env_initialized) env_init();
    for (int i = 0; i < env_count; i++) {
        if (se_strcmp(env_vars[i].name, name) == 0) {
            return env_vars[i].val;
        }
    }
    return 0;
}

void env_unset(const char *name)
{
    if (!env_initialized) env_init();
    for (int i = 0; i < env_count; i++) {
        if (se_strcmp(env_vars[i].name, name) == 0) {
            for (int j = i; j < env_count - 1; j++) env_vars[j] = env_vars[j + 1];
            env_count--;
            return;
        }
    }
}

void env_list(void)
{
    if (!env_initialized) env_init();
    for (int i = 0; i < env_count; i++) {
        vga_print_color(env_vars[i].name, 0x0B);
        vga_print("=");
        vga_print(env_vars[i].val);
        vga_print("\n");
    }
}

static void env_expand_str(const char *in, char *out, int out_sz)
{
    if (!env_initialized) env_init();
    int oi = 0;
    for (int i = 0; in[i] && oi < out_sz - 1; i++) {
        if (in[i] == '$' && ((in[i+1] >= 'A' && in[i+1] <= 'Z') || (in[i+1] >= 'a' && in[i+1] <= 'z') || in[i+1] == '_')) {
            i++;
            char varname[ENV_NAMELEN];
            int vi = 0;
            while (in[i] && ((in[i] >= 'A' && in[i] <= 'Z') || (in[i] >= 'a' && in[i] <= 'z') || (in[i] >= '0' && in[i] <= '9') || in[i] == '_') && vi < ENV_NAMELEN - 1) {
                varname[vi++] = in[i++];
            }
            varname[vi] = '\0';
            i--; /* Backtrack since loop increments */

            const char *val = env_get(varname);
            if (val) {
                while (*val && oi < out_sz - 1) {
                    out[oi++] = *val++;
                }
            }
        } else {
            out[oi++] = in[i];
        }
    }
    out[oi] = '\0';
}

/* ============================================================
 * wc COMMAND
 * ============================================================ */

static void cmd_wc(const char *path)
{
    char buf[2048];
    if (fs_cat(path, buf, sizeof(buf)) < 0) {
        vga_print("wc: no such file\n"); return;
    }
    int lines=0, words=0, chars=0;
    int in_word=0;
    for (int i=0;buf[i];i++) {
        chars++;
        if (buf[i]=='\n') lines++;
        if (buf[i]==' '||buf[i]=='\n'||buf[i]=='\t') in_word=0;
        else if (!in_word) { in_word=1; words++; }
    }
    char tmp[16];
    vga_print("  lines: "); itoa(lines, tmp, 10); vga_print(tmp);
    vga_print("  words: "); itoa(words, tmp, 10); vga_print(tmp);
    vga_print("  chars: "); itoa(chars, tmp, 10); vga_print(tmp);
    vga_print("\n");
}

/* ============================================================
 * grep COMMAND
 * ============================================================ */

static void cmd_grep(const char *pattern, const char *path)
{
    char buf[2048];
    if (fs_cat(path, buf, sizeof(buf)) < 0) {
        vga_print("grep: no such file\n"); return;
    }
    int plen = se_strlen(pattern);
    int found = 0;

    /* Walk line by line */
    int i=0;
    while (buf[i]) {
        /* Find end of line */
        int j=i;
        while (buf[j]&&buf[j]!='\n') j++;

        /* Search pattern in line [i..j) */
        int hit=0;
        for (int k=i; k<j-plen+1; k++) {
            if (se_strncmp(buf+k, pattern, plen)==0) { hit=1; break; }
        }
        if (hit) {
            /* Print the line */
            for (int k=i;k<j;k++) vga_print_char(buf[k]);
            vga_print("\n");
            found++;
        }
        i = buf[j] ? j+1 : j;
    }
    if (!found) vga_print("(no matches)\n");
}

/* ============================================================
 * SCRIPT RUNNER
 * ============================================================ */

void script_run(const char *path)
{
    char buf[2048];
    if (fs_cat(path, buf, sizeof(buf)) < 0) {
        vga_print("run: no such file\n"); return;
    }

    /* Execute each line as a shell command */
    int i=0;
    while (buf[i]) {
        char line[128]; int li=0;
        while (buf[i] && buf[i] != '\n') {
            if (li < 127) line[li++] = buf[i];
            i++;
        }
        line[li]='\0';
        if (buf[i]=='\n') i++;
        if (li==0||line[0]=='#') continue;  /* skip empty/comment lines */
        vga_print_color(">> ", 0x08);
        vga_print(line); vga_print("\n");
        shell_exec(line);
    }
}

/* ============================================================
 * OUTPUT REDIRECTION HELPER
 * Runs cmd with output captured, then writes to file.
 * mode: 0 = overwrite (>), 1 = append (>>)
 * ============================================================ */

static char redir_existing[CAP_SIZE];
static char redir_combined[CAP_SIZE * 2];

static void exec_with_redirect(const char *cmd, const char *file, int append)
{
    shellext_capture_start();
    shell_exec(cmd);
    shellext_capture_stop();

    if (append) {
        /* Read existing content */
        redir_existing[0] = '\0';
        fs_cat(file, redir_existing, sizeof(redir_existing));
        int el = se_strlen(redir_existing);
        /* Append new content */
        int ci = 0;
        for (int i = 0; i < el && ci < (int)(sizeof(redir_combined) - 1); i++)
            redir_combined[ci++] = redir_existing[i];
        for (int i = 0; i < cap_len && ci < (int)(sizeof(redir_combined) - 1); i++)
            redir_combined[ci++] = cap_buf[i];
        redir_combined[ci] = '\0';
        fs_write(file, redir_combined, (size_t)ci);
    } else {
        fs_write(file, cap_buf, (size_t)cap_len);
    }
}

/* ============================================================
 * PIPE HELPER
 * Runs left side with capture, feeds output as input to right.
 * Currently supports: <cmd> | grep <pattern>
 *                     <cmd> | wc
 * ============================================================ */

static void exec_pipe(const char *left, const char *right)
{
    /* Capture left side output into a temp file */
    static const char *TMPFILE = "/tmp_pipe";

    shellext_capture_start();
    shell_exec(left);
    shellext_capture_stop();

    /* Write captured output to temp file */
    fs_write(TMPFILE, cap_buf, (size_t)cap_len);

    /* Build right-side command with temp file */
    char right_cmd[128];
    se_strncpy(right_cmd, right, sizeof(right_cmd));

    /* Trim leading spaces */
    int r = 0; while (right_cmd[r] == ' ') r++;
    const char *rcmd = right_cmd + r;

    /* Handle pipe consumers */
    extern void cmd_less(const char *path);
    if (se_strncmp(rcmd, "grep ", 5) == 0) {
        cmd_grep(rcmd + 5, TMPFILE);
    } else if (se_strcmp(rcmd, "wc") == 0) {
        cmd_wc(TMPFILE);
    } else if (se_strcmp(rcmd, "less") == 0 || se_strcmp(rcmd, "more") == 0) {
        cmd_less(TMPFILE);
    } else if (se_strncmp(rcmd, "less ", 5) == 0 || se_strncmp(rcmd, "more ", 5) == 0) {
        shell_exec(rcmd);
    } else {
        /* General command consumer: append TMPFILE if right command has no arguments */
        char full_right[160];
        int ri = 0;
        while (rcmd[ri] && ri < 120) { full_right[ri] = rcmd[ri]; ri++; }
        int has_space = 0;
        for (int i = 0; i < ri; i++) {
            if (full_right[i] == ' ') { has_space = 1; break; }
        }
        if (!has_space) {
            full_right[ri++] = ' ';
            const char *t = TMPFILE;
            while (*t && ri < 159) full_right[ri++] = *t++;
            full_right[ri] = '\0';
            shell_exec(full_right);
        } else {
            shell_exec(rcmd);
        }
    }

    fs_rm(TMPFILE);
}

/* ============================================================
 * SHELL_EXEC — main entry point
 * Preprocesses command then dispatches.
 * ============================================================ */

static int exec_depth = 0;
#define MAX_EXEC_DEPTH 8

static void shell_exec_internal(const char *raw)
{
    /* Skip empty */
    if (!raw||!raw[0]) return;

    /* Trim leading spaces */
    while (*raw==' ') raw++;
    if (!*raw) return;

    /* ── Semicolon command separator ';' ── */
    int semi_pos = -1;
    int in_q = 0;
    for (int i = 0; raw[i]; i++) {
        if (raw[i] == '"' || raw[i] == '\'') in_q = !in_q;
        else if (raw[i] == ';' && !in_q) { semi_pos = i; break; }
    }
    if (semi_pos >= 0) {
        char first[128], second[128];
        int fi = 0;
        for (int i = 0; i < semi_pos && fi < 127; i++) first[fi++] = raw[i];
        while (fi > 0 && first[fi - 1] == ' ') fi--;
        first[fi] = '\0';

        int si = 0;
        const char *s = raw + semi_pos + 1;
        while (*s == ' ') s++;
        while (*s && si < 127) second[si++] = *s++;
        second[si] = '\0';

        if (first[0]) shell_exec(first);
        if (second[0]) shell_exec(second);
        return;
    }

    /* Expand environment variables $VAR */
    char env_expanded[256];
    env_expand_str(raw, env_expanded, sizeof(env_expanded));

    /* Alias expansion */
    const char *expanded = alias_expand(env_expanded);
    const char *cmd = expanded ? expanded : env_expanded;

    /* ── export command ── */
    if (se_strncmp(cmd, "export ", 7) == 0) {
        const char *rest = cmd + 7;
        while (*rest == ' ') rest++;
        int ei = 0; while (rest[ei] && rest[ei] != '=') ei++;
        if (!rest[ei]) {
            vga_print("usage: export NAME=VALUE\n");
            return;
        }
        char name[ENV_NAMELEN]; int ni = 0;
        while (ni < ei && ni < ENV_NAMELEN - 1) { name[ni] = rest[ni]; ni++; }
        name[ni] = '\0';
        while (ni > 0 && name[ni - 1] == ' ') name[--ni] = '\0';
        if (ni == 0) { vga_print("export: invalid variable name\n"); return; }
        env_set(name, rest + ei + 1);
        return;
    }
    if (se_strcmp(cmd, "export") == 0 || se_strcmp(cmd, "env") == 0) {
        env_list();
        return;
    }

    /* ── unset command ── */
    if (se_strncmp(cmd, "unset ", 6) == 0) {
        const char *name = cmd + 6;
        while (*name == ' ') name++;
        env_unset(name);
        return;
    }
    if (se_strcmp(cmd, "unset") == 0) {
        vga_print("usage: unset <NAME>\n");
        return;
    }

    /* ── alias command ── */
    if (se_strncmp(cmd, "alias ", 6)==0) {
        /* alias name=value */
        const char *rest = cmd+6;
        while(*rest==' ') rest++;
        /* Find = */
        int ei=0; while(rest[ei]&&rest[ei]!='=') ei++;
        if (!rest[ei]) { vga_print("usage: alias name=value\n"); return; }
        char name[ALIAS_NAMELEN]; int ni=0;
        while(ni<ei&&ni<ALIAS_NAMELEN-1){name[ni]=rest[ni];ni++;} name[ni]='\0';
        while (ni > 0 && name[ni-1] == ' ') name[--ni] = '\0';
        if (ni == 0) { vga_print("alias: invalid alias name\n"); return; }
        alias_set(name, rest+ei+1);
        return;
    }

    if (se_strcmp(cmd, "alias")==0) { alias_list(); return; }

    /* ── unalias ── */
    if (se_strncmp(cmd, "unalias ", 8)==0) {
        const char *name=cmd+8;
        while(*name==' ') name++;
        for(int i=0;i<alias_count;i++) {
            if(se_strcmp(aliases[i].name,name)==0) {
                for(int j=i;j<alias_count-1;j++) aliases[j]=aliases[j+1];
                alias_count--;
                vga_print("alias removed\n"); return;
            }
        }
        vga_print("unalias: not found\n"); return;
    }
    if (se_strcmp(cmd, "unalias")==0) { vga_print("usage: unalias <name>\n  Removes a command alias.\n"); return; }

    /* ── wc ── */
    if (se_strncmp(cmd, "wc ", 3)==0) { cmd_wc(cmd+3); return; }
    if (se_strcmp(cmd, "wc")==0) { vga_print("usage: wc <file>\n  Counts lines, words, and characters in a file.\n"); return; }

    /* ── grep ── */
    if (se_strncmp(cmd, "grep ", 5)==0) {
        const char *rest=cmd+5;
        /* grep <pattern> <file> */
        while(*rest==' ') rest++;
        /* find space between pattern and file */
        int pi=0; while(rest[pi]&&rest[pi]!=' ') pi++;
        if(!rest[pi]){vga_print("usage: grep <pattern> <file>\n");return;}
        char pat[64]; int pj=0;
        while(pj<pi&&pj<63){pat[pj]=rest[pj];pj++;} pat[pj]='\0';
        const char *file=rest+pi+1;
        while(*file==' ') file++;
        cmd_grep(pat, file);
        return;
    }
    if (se_strcmp(cmd, "grep")==0) { vga_print("usage: grep <pattern> <file>\n  Searches for a text pattern in a file.\n"); return; }

    /* ── history ── */
    if (se_strcmp(cmd, "history")==0) {
        extern void vga_print_history(void);
        vga_print_history();
        return;
    }

    /* ── run (script) ── */
    if (se_strncmp(cmd, "run ", 4)==0) { script_run(cmd+4); return; }
    if (se_strcmp(cmd, "run")==0) { vga_print("usage: run <script>\n  Executes shell commands from a script file.\n"); return; }

    /* ── Scan for pipe | ── */
    /* Find | not inside quotes */
    int pipe_pos = -1;
    for (int i=0; cmd[i]; i++) {
        if (cmd[i]=='|') { pipe_pos=i; break; }
    }
    if (pipe_pos >= 0) {
        char left[128], right[128];
        int li=0;
        for(int i=0;i<pipe_pos&&li<127;i++) left[li++]=cmd[i];
        /* trim trailing space */
        while(li>0&&left[li-1]==' ') li--;
        left[li]='\0';
        int ri=0;
        const char *r=cmd+pipe_pos+1;
        while(*r==' ') r++;
        while(*r&&ri<127) right[ri++]=*r++;
        right[ri]='\0';
        exec_pipe(left, right);
        return;
    }

    /* ── Scan for >> before > ── */
    int redir_pos = -1;
    int redir_append = 0;
    for (int i=0; cmd[i]; i++) {
        if (cmd[i]=='>'&&cmd[i+1]=='>') { redir_pos=i; redir_append=1; break; }
        if (cmd[i]=='>') { redir_pos=i; redir_append=0; break; }
    }
    if (redir_pos >= 0) {
        char left[128], file[FS_MAX_NAME];
        int li=0;
        for(int i=0;i<redir_pos&&li<127;i++) left[li++]=cmd[i];
        while(li>0&&left[li-1]==' ') li--;
        left[li]='\0';
        const char *f = cmd+redir_pos+(redir_append?2:1);
        while(*f==' ') f++;
        int fi=0; while(*f&&fi<FS_MAX_NAME-1) file[fi++]=*f++;
        while(fi>0&&file[fi-1]==' ') fi--;
        file[fi]='\0';
        exec_with_redirect(left, file, redir_append);
        return;
    }

    /* ── Plain command ── */
    kernel_execute_command(cmd);
}

void shell_exec(const char *raw)
{
    if (exec_depth >= MAX_EXEC_DEPTH) {
        vga_print("shell: maximum recursion depth exceeded\n");
        return;
    }
    exec_depth++;
    shell_exec_internal(raw);
    exec_depth--;
}

/* ============================================================
 * CLIPBOARD & SERIAL COM1 SYNC
 * ============================================================ */
static char clipboard_buf[2048] = "";
static int  clipboard_len = 0;

void clipboard_copy(const char *text) {
    if (!text) return;
    int i = 0;
    while (text[i] && i < 2047) {
        clipboard_buf[i] = text[i];
        i++;
    }
    clipboard_buf[i] = '\0';
    clipboard_len = i;
    /* Stream out to Serial COM1 for host capture */
    serial_puts(COM1_BASE, clipboard_buf);
    serial_putc(COM1_BASE, '\n');
}

const char *clipboard_paste(void) {
    return clipboard_buf;
}

void clipboard_check_serial_input(void) {
    while (serial_data_ready(COM1_BASE)) {
        char c = serial_try_getc(COM1_BASE);
        if (c == 0) break;
        if (c == '\r') c = '\n';
        if (clipboard_len < 2047) {
            clipboard_buf[clipboard_len++] = c;
            clipboard_buf[clipboard_len] = '\0';
        }
    }
}
