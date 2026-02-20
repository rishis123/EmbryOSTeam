#include "embryos.h"

void syscall_handler(struct trap_frame *tf)
{
    struct pcb *self = sched_self();
    extern struct flat flat_fs;

    switch (tf->a7)
    {
    case SYS_EXIT:
        L0(L_NORM, L_USER_EXIT);
        die("process ended");
        break;
    case SYS_YIELD:
        L0(L_NORM, L_USER_YIELD);
        break;
    case SYS_SPAWN:
        L1(L_NORM, L_USER_SPAWN, tf->a0);
        proc_check_legal(self, (uintptr_t)tf->a5, tf->a6);
        if (tf->a6 > PAGE_SIZE)
            die("too many arguments");
        sched_run(tf->a0, (struct rect){tf->a1, tf->a2, tf->a3, tf->a4}, (void *)(uintptr_t)tf->a5, tf->a6);
        break;
    case SYS_PUT:
        L3(L_FREQ, L_USER_PUT, tf->a0, tf->a1, tf->a2);
        proc_put(self, tf->a0, tf->a1, tf->a2);
        break;
    case SYS_GET:
        L1(L_NORM, L_USER_GET, tf->a0);
        tf->a0 = io_get(self, tf->a0);
        break;
    case SYS_CREATE:
        L0(L_NORM, L_USER_CREATE);
        tf->a0 = flat_create(&flat_fs);
        break;
    case SYS_READ:
        L4(L_NORM, L_USER_READ, tf->a0, tf->a1, tf->a2, tf->a3);
        proc_check_legal(self, (uintptr_t)tf->a2, tf->a3);
        tf->a0 = flat_read(&flat_fs, tf->a0, tf->a1, (void *)(uintptr_t)tf->a2, tf->a3);
        break;
    case SYS_WRITE:
        L4(L_NORM, L_USER_WRITE, tf->a0, tf->a1, tf->a2, tf->a3);
        proc_check_legal(self, (uintptr_t)tf->a2, tf->a3);
        tf->a0 = flat_write(&flat_fs, tf->a0, tf->a1, (void *)(uintptr_t)tf->a2, tf->a3);
        break;
    case SYS_SIZE:
        L1(L_NORM, L_USER_SIZE, tf->a0);
        tf->a0 = flat_size(&flat_fs, tf->a0);
        break;
    case SYS_DELETE:
        L1(L_NORM, L_USER_DELETE, tf->a0);
        flat_delete(&flat_fs, tf->a0);
        break;
    case SYS_GETTIME:
        // Used Github Copilot to help understand why the following two declarations are needed.
        extern uint64_t mtime_get(void);
        extern uint64_t time_base;

        // gives us current time in ticks since boot.
        uint64_t ticks = mtime_get();
        // Used Github Copilot to understand what a trap frame is (holder for CPU registers). a0 is return register here.
        tf->a0 = (ticks * 1000000000ULL) / time_base;
        // Computation -> ticks * 1,000,000,000 / ticks per second = number of nanoseconds since boot. Used Copilot advice regarding ULL to avoid overflow/compiler issues.
        break;
    case SYS_SLEEP:
        // compute sleep time and make process sleep
        uint64_t deadline = tf->a0;

        // Used Github Copilot to help understand why the following two declarations are needed.
        extern uint64_t mtime_get(void);
        extern uint64_t time_base;

        // gives us current time in ticks since boot.
        uint64_t curr_time = mtime_get();
        uint64_t curr_time_ns;
        // Used Github Copilot to understand what a trap frame is (holder for CPU registers). a0 is return register here.
        curr_time_ns = (curr_time * 1000000000ULL) / time_base;
        // Computation -> ticks * 1,000,000,000 / ticks per second = number of nanoseconds since boot. Used Copilot advice regarding ULL to avoid overflow/compiler issues.

        if (curr_time_ns >= deadline)
        {
            break; // break if user_gettime() >= deadline
        }
        else
        {
            self->sleep_deadline = deadline;
            self->sleeping = 1;

            sched_sleep(self);
            sched_block(self);
            break;
        }
    case 56:
    case 63:
    case 64:
    case 93:
    case 214:
        selfie_syscall_handler(tf);
        break;
    default:
        die("unknown system call");
    }
    tf->sepc += 4; // skip ecall
}
