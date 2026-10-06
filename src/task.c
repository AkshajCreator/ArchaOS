#include "task.h"
#include "mm.h"
#include "pit.h"
#include "serial.h"
#include "vmm.h"
#include "gdt.h"
#include "coreview.h"
#include "string.h"

static task_t tasks[MAX_TASKS];
static task_t *current_task = NULL;
static uint32_t next_pid = 1;
static int scheduler_enabled = 0;

void task_init(void)
{
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_UNUSED;
        tasks[i].pid = 0;
        tasks[i].name[0] = '\0';
        tasks[i].esp = 0;
        tasks[i].stack_base = 0;
        tasks[i].stack_size = 0;
        tasks[i].priority = 1;
        tasks[i].time_slice = DEFAULT_TIME_SLICE;
        tasks[i].total_ticks = 0;
        tasks[i].sleep_until_ticks = 0;
        tasks[i].last_heartbeat = 0;
        tasks[i].win_id = -1;
        tasks[i].watchdog_prompted = 0;
        tasks[i].is_user = 0;
        tasks[i].page_dir = NULL;
        tasks[i].user_stack_top = 0;
        tasks[i].user_brk = 0;
        tasks[i].exit_code = 0;
        tasks[i].next = NULL;
    }

    /* Task 0: Main kernel execution thread */
    tasks[0].pid = 0;
    strncpy(tasks[0].name, "kernel_main", sizeof(tasks[0].name));
    tasks[0].state = TASK_RUNNING;
    tasks[0].priority = 3;
    tasks[0].time_slice = DEFAULT_TIME_SLICE * 3;
    tasks[0].total_ticks = 0;
    tasks[0].stack_base = 0;
    tasks[0].stack_size = 0;
    tasks[0].win_id = -1;
    tasks[0].last_heartbeat = pit_ticks();
    tasks[0].watchdog_prompted = 0;
    tasks[0].is_user = 0;
    tasks[0].page_dir = NULL;
    tasks[0].user_stack_top = 0;
    tasks[0].user_brk = 0;
    tasks[0].exit_code = 0;

    current_task = &tasks[0];
    scheduler_enabled = 1;

    serial_puts(COM1_BASE, "[TASK] Preemptive Multitasking & Priority Scheduler initialized (Max tasks: 32)\n");
}

task_t *task_create(const char *name, void (*entry)(void), uint32_t priority, int win_id)
{
    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED || tasks[i].state == TASK_TERMINATED) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        serial_puts(COM1_BASE, "[TASK] Error: Process table full, cannot create task!\n");
        return NULL;
    }

    uint8_t *stack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    if (!stack) {
        serial_puts(COM1_BASE, "[TASK] Error: Out of memory allocating task stack!\n");
        return NULL;
    }

    task_t *t = &tasks[slot];
    t->pid = next_pid++;
    strncpy(t->name, name ? name : "task", sizeof(t->name));
    t->stack_base = (uint32_t)stack;
    t->stack_size = TASK_STACK_SIZE;
    t->state = TASK_READY;

    if (priority < 1) priority = 1;
    if (priority > 5) priority = 5;
    t->priority = priority;
    t->time_slice = DEFAULT_TIME_SLICE * priority;
    t->total_ticks = 0;
    t->sleep_until_ticks = 0;
    t->last_heartbeat = pit_ticks();
    t->win_id = win_id;
    t->watchdog_prompted = 0;
    t->is_user = 0;
    t->page_dir = NULL;
    t->user_stack_top = 0;
    t->user_brk = 0;
    t->exit_code = 0;

    uint32_t stack_top = (uint32_t)(stack + TASK_STACK_SIZE);
    uint32_t *stk = (uint32_t *)stack_top;

    *(--stk) = (uint32_t)task_exit;
    *(--stk) = 0x0202;
    *(--stk) = 0x08;
    *(--stk) = (uint32_t)entry;
    *(--stk) = 0; /* EAX */
    *(--stk) = 0; /* ECX */
    *(--stk) = 0; /* EDX */
    *(--stk) = 0; /* EBX */
    *(--stk) = 0; /* ESP */
    *(--stk) = stack_top; /* EBP */
    *(--stk) = 0; /* ESI */
    *(--stk) = 0; /* EDI */
    *(--stk) = 0x10; /* DS */
    *(--stk) = 0x10; /* ES */
    *(--stk) = 0x10; /* FS */
    *(--stk) = 0x10; /* GS */

    t->esp = (uint32_t)stk;

    serial_printf(COM1_BASE, "[TASK] Created kernel task '%s' (PID %u, Priority %u, ESP 0x%x)\n",
                  t->name, t->pid, t->priority, t->esp);

    return t;
}

task_t *task_create_user(const char *name, uint32_t entry_point, uint32_t user_stack_top, uint32_t *pagedir, uint32_t priority)
{
    int slot = -1;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED || tasks[i].state == TASK_TERMINATED) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        serial_puts(COM1_BASE, "[TASK] Error: Process table full, cannot create user task!\n");
        return NULL;
    }

    uint8_t *kstack = (uint8_t *)kmalloc(TASK_STACK_SIZE);
    if (!kstack) {
        serial_puts(COM1_BASE, "[TASK] Error: Out of memory allocating user task kernel stack!\n");
        return NULL;
    }

    task_t *t = &tasks[slot];
    t->pid = next_pid++;
    strncpy(t->name, name ? name : "user_task", sizeof(t->name));
    t->stack_base = (uint32_t)kstack;
    t->stack_size = TASK_STACK_SIZE;
    t->state = TASK_READY;

    if (priority < 1) priority = 1;
    if (priority > 5) priority = 5;
    t->priority = priority;
    t->time_slice = DEFAULT_TIME_SLICE * priority;
    t->total_ticks = 0;
    t->sleep_until_ticks = 0;
    t->last_heartbeat = pit_ticks();
    t->win_id = -1;
    t->watchdog_prompted = 0;
    t->is_user = 1;
    t->page_dir = pagedir;
    t->user_stack_top = user_stack_top;
    t->user_brk = 0x20000000;
    t->exit_code = 0;

    /* Synthesize user iret stack frame on kernel stack */
    uint32_t stack_top = (uint32_t)(kstack + TASK_STACK_SIZE);
    uint32_t *stk = (uint32_t *)stack_top;

    *(--stk) = 0x33;              /* User SS (0x30 | 3) */
    *(--stk) = user_stack_top;    /* User ESP */
    *(--stk) = 0x0202;            /* EFLAGS: IF=1 */
    *(--stk) = 0x2B;              /* User CS (0x28 | 3) */
    *(--stk) = entry_point;       /* EIP */
    *(--stk) = 0;                 /* EAX */
    *(--stk) = 0;                 /* ECX */
    *(--stk) = 0;                 /* EDX */
    *(--stk) = 0;                 /* EBX */
    *(--stk) = 0;                 /* ESP dummy */
    *(--stk) = user_stack_top;    /* EBP */
    *(--stk) = 0;                 /* ESI */
    *(--stk) = 0;                 /* EDI */
    *(--stk) = 0x33;              /* DS: User Data */
    *(--stk) = 0x33;              /* ES: User Data */
    *(--stk) = 0x33;              /* FS: User Data */
    *(--stk) = 0x33;              /* GS: User Data */

    t->esp = (uint32_t)stk;

    serial_printf(COM1_BASE, "[TASK] Created Ring 3 User Task '%s' (PID %u, Entry 0x%x, UserESP 0x%x, CR3 0x%x)\n",
                  t->name, t->pid, entry_point, user_stack_top, (uint32_t)pagedir);

    return t;
}

void task_yield(void)
{
    asm volatile ("int $0x20");
}

void task_sleep(uint32_t ms)
{
    if (!task_is_scheduler_running()) {
        pit_sleep(ms);
        return;
    }

    uint32_t wake_ticks = pit_ticks() + ms;
    current_task->sleep_until_ticks = wake_ticks;
    current_task->state = TASK_SLEEPING;
    while (pit_ticks() < wake_ticks) {
        task_yield();
    }
}

void task_kill(uint32_t pid)
{
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].pid == pid && tasks[i].state != TASK_UNUSED && tasks[i].state != TASK_TERMINATED) {
            serial_printf(COM1_BASE, "[TASK] Killing task '%s' (PID %u)\n", tasks[i].name, tasks[i].pid);
            if (tasks[i].stack_base != 0) {
                kfree((void *)tasks[i].stack_base);
                tasks[i].stack_base = 0;
            }
            if (tasks[i].is_user && tasks[i].page_dir) {
                vmm_destroy_user_pagedir(tasks[i].page_dir);
                tasks[i].page_dir = NULL;
            }
            tasks[i].state = TASK_TERMINATED;

            if (current_task && current_task->pid == pid) {
                task_yield();
            }
            return;
        }
    }
}

void task_exit(void)
{
    if (current_task) {
        task_kill(current_task->pid);
    }
    for (;;) {
        asm volatile ("hlt");
    }
}

task_t *task_get_current(void)
{
    return current_task;
}

int task_count(void)
{
    int count = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state != TASK_UNUSED && tasks[i].state != TASK_TERMINATED) {
            count++;
        }
    }
    return count;
}

task_t *task_get_by_index(int idx)
{
    if (idx >= 0 && idx < MAX_TASKS) {
        return &tasks[idx];
    }
    return NULL;
}

int task_is_running(uint32_t pid)
{
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].pid == pid && tasks[i].state != TASK_UNUSED && tasks[i].state != TASK_TERMINATED) {
            return 1;
        }
    }
    return 0;
}

int task_is_scheduler_running(void)
{
    return (scheduler_enabled && current_task != NULL) ? 1 : 0;
}

void task_heartbeat(uint32_t pid)
{
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].pid == pid && tasks[i].state != TASK_UNUSED && tasks[i].state != TASK_TERMINATED) {
            tasks[i].last_heartbeat = pit_ticks();
            tasks[i].watchdog_prompted = 0;
            return;
        }
    }
}

int task_check_watchdog(char *out_hung_name, uint32_t *out_hung_pid)
{
    uint32_t now = pit_ticks();

    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state != TASK_UNUSED && tasks[i].state != TASK_TERMINATED && tasks[i].win_id >= 0) {
            if (!tasks[i].watchdog_prompted && (now - tasks[i].last_heartbeat > WATCHDOG_TIMEOUT_MS)) {
                tasks[i].watchdog_prompted = 1;
                if (out_hung_name) {
                    strncpy(out_hung_name, tasks[i].name, 32);
                }
                if (out_hung_pid) {
                    *out_hung_pid = tasks[i].pid;
                }
                return 1;
            }
        }
    }
    return 0;
}

void task_watchdog_extend(uint32_t pid)
{
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].pid == pid) {
            tasks[i].last_heartbeat = pit_ticks();
            tasks[i].watchdog_prompted = 0;
            return;
        }
    }
}

uint32_t scheduler_schedule(uint32_t current_esp)
{
    /* Capture live interrupted CPU execution for CoreView Silicon Monitor */
    coreview_capture_irq_cpu((const void *)current_esp);

    if (!scheduler_enabled || !current_task) {
        return current_esp;
    }

    uint32_t now = pit_ticks();

    /* 1. Wake up sleeping tasks whose sleep timer has expired */
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEPING) {
            if (now >= tasks[i].sleep_until_ticks) {
                tasks[i].state = TASK_READY;
            }
        }
    }

    /* 2. Check if current task can continue running in this quantum */
    if (current_task->state == TASK_RUNNING) {
        if (current_task->time_slice > 0) {
            current_task->time_slice--;
            current_task->total_ticks++;
            return current_esp;
        }
        /* Time slice exhausted: transition to READY */
        current_task->state = TASK_READY;
        current_task->time_slice = DEFAULT_TIME_SLICE * current_task->priority;
    }

    /* Save current task's ESP */
    current_task->esp = current_esp;

    /* 3. Round-Robin search for the next READY task */
    int curr_idx = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (&tasks[i] == current_task) {
            curr_idx = i;
            break;
        }
    }

    task_t *next_task = NULL;
    for (int i = 1; i <= MAX_TASKS; i++) {
        int idx = (curr_idx + i) % MAX_TASKS;
        if (tasks[idx].state == TASK_READY) {
            next_task = &tasks[idx];
            break;
        }
    }

    /* If no other ready task found, fallback to current or task 0 */
    if (!next_task) {
        if (current_task->state == TASK_READY) {
            next_task = current_task;
        } else {
            next_task = &tasks[0];
        }
    }

    /* 4. Switch to next task */
    if (next_task != current_task) {
        serial_printf(COM1_BASE, "[SCHED] Switching to task '%s' (PID %u, is_user=%d, ESP=0x%x, CR3=0x%x)\n",
                      next_task->name, next_task->pid, next_task->is_user, next_task->esp, (uint32_t)next_task->page_dir);
    }
    next_task->state = TASK_RUNNING;
    next_task->total_ticks++;
    current_task = next_task;

    /* Update Kernel TSS esp0 for privilege transitions from Ring 3 */
    if (next_task->stack_base != 0) {
        tss_set_kernel_stack(next_task->stack_base + next_task->stack_size);
    }

    /* Switch CR3 page directory if task has a dedicated user page directory */
    if (next_task->page_dir) {
        vmm_switch_pagedir(next_task->page_dir);
    } else {
        vmm_switch_pagedir(vmm_get_kernel_pagedir());
    }

    return current_task->esp;
}
