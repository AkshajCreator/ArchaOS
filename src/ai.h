#ifndef AI_H
#define AI_H

#include <stddef.h>

void ai_init(void);
void ai_chat(void);
void ai_get_response(const char *input, char *output, size_t max_len);

#endif
