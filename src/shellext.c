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
#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * STRING HELPERS
 * ============================================================ */

static int se_strlen(const char *s)
{ int n=0; while(s[n]) n++; return n; }

static void se_strcpy(char *d, const char *s)
{ while((*d++=*s++)); }

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

static void capture_start(void)
{
    cap_len   = 0;
    cap_buf[0]= '\0';
    capturing = 1;
}

static void capture_stop(void)
{
    capturing   = 0;
    cap_buf[cap_len] = '\0';
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
    char tmp[8];
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
        while (buf[i]&&buf[i]!='\n'&&li<127) line[li++]=buf[i++];
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
    capture_start();
    kernel_execute_command(cmd);
    capture_stop();

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

    capture_start();
    kernel_execute_command(left);
    capture_stop();

    /* Write captured output to temp file */
    fs_write(TMPFILE, cap_buf, (size_t)cap_len);

    /* Build right-side command with temp file */
    char right_cmd[128];
    se_strcpy(right_cmd, right);

    /* Trim leading spaces */
    int r=0; while(right_cmd[r]==' ') r++;
    const char *rcmd = right_cmd + r;

    /* Handle known pipe consumers */
    if (se_strncmp(rcmd, "grep ", 5)==0) {
        cmd_grep(rcmd+5, TMPFILE);
    } else if (se_strcmp(rcmd, "wc")==0) {
        cmd_wc(TMPFILE);
    } else {
        vga_print("pipe: right side must be grep or wc\n");
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

    /* Alias expansion */
    const char *expanded = alias_expand(raw);
    const char *cmd = expanded ? expanded : raw;

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
        alias_set(name, rest+ei+1);
        return;
    }

    if (se_strcmp(cmd, "alias")==0) { alias_list(); return; }

    /* ── unalias ── */
    if (se_strncmp(cmd, "unalias ", 8)==0) {
        const char *name=cmd+8;
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
