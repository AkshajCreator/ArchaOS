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

#endif
