// src/shellext.h — shell extensions: aliases, redirection, pipes, script runner
#ifndef SHELLEXT_H
#define SHELLEXT_H

// Call this instead of kernel_execute_command — handles all preprocessing
void shell_exec(const char *cmd);

// Alias management
void alias_set(const char *name, const char *value);
void alias_list(void);

// Script runner
void script_run(const char *path);

// Capture buffer
void shellext_capture_char(char c);
void shellext_capture_start(void);
void shellext_capture_stop(void);
int  shellext_is_capturing(void);
const char *shellext_get_captured(void);

// Environment variables
void env_init(void);
void env_set(const char *name, const char *value);
const char *env_get(const char *name);
void env_unset(const char *name);
void env_list(void);

// Clipboard & Serial COM1 sync
void clipboard_copy(const char *text);
const char *clipboard_paste(void);
void clipboard_check_serial_input(void);

#endif
