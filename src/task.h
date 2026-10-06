#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stddef.h>

#define MAX_TASKS           32
#define TASK_STACK_SIZE     (32 * 1024)   /* 32 KB kernel stack per task */
#define DEFAULT_TIME_SLICE  10            /* 10 timer ticks (~10ms) per quantum */
#define WATCHDOG_TIMEOUT_MS 3000          /* 3 seconds without heartbeat = not responding */

typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_BLOCKED,
    TASK_TERMINATED
} task_state_t;

typedef struct task {
    uint32_t         pid;
    char             name[32];
    uint32_t         esp;               /* Saved stack pointer during context switch */
    uint32_t         stack_base;        /* Base address of allocated stack */
    uint32_t         stack_size;        /* Size in bytes */
    task_state_t     state;
    uint32_t         priority;          /* 1 (Lowest) to 5 (Highest) */
    uint32_t         time_slice;        /* Remaining ticks in current slice */
    uint32_t         total_ticks;       /* Lifetime CPU ticks consumed */
    uint32_t         sleep_until_ticks; /* Sleep expiration in pit_ticks */
    uint32_t         last_heartbeat;    /* Last active timestamp for Watchdog */
    int              win_id;            /* Associated GUI window (-1 if background/CLI) */
    int              watchdog_prompted; /* 1 if not responding dialog is currently shown */
    int              is_user;           /* 1 if Ring 3 User Mode task */
    uint32_t        *page_dir;          /* User page directory (or NULL if kernel) */
    uint32_t         user_stack_top;    /* Top of user mode stack */
    uint32_t         user_brk;          /* Current user heap break */
    int              exit_code;
    struct task     *next;
} task_t;

/* Scheduler API */
void     task_init(void);
task_t  *task_create(const char *name, void (*entry)(void), uint32_t priority, int win_id);
task_t  *task_create_user(const char *name, uint32_t entry_point, uint32_t user_stack_top, uint32_t *pagedir, uint32_t priority);
void     task_yield(void);
void     task_sleep(uint32_t ms);
void     task_kill(uint32_t pid);
void     task_exit(void);
task_t  *task_get_current(void);
int      task_count(void);
task_t  *task_get_by_index(int idx);
int      task_is_running(uint32_t pid);
int      task_is_scheduler_running(void);

/* Watchdog API */
void     task_heartbeat(uint32_t pid);
int      task_check_watchdog(char *out_hung_name, uint32_t *out_hung_pid);
void     task_watchdog_extend(uint32_t pid);

/* Context Switcher Hook (invoked from PIT IRQ0 assembly stub) */
uint32_t scheduler_schedule(uint32_t current_esp);

#endif /* TASK_H */
