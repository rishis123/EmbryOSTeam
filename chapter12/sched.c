#include "embryos.h"

#define N_PRIORITIES 3

// This is an array of cricular linked lists of runnable processes.
// Each index is a different priority (0 high to 2 low).
// Runs next high priority available process in run_queue. If there is higher priority available, always runs that first.
struct pcb *run_queue[N_PRIORITIES];
// Single circular linked list of which processes are sleeping.
struct pcb *sleep_queue = NULL;

// adds this process (encoded in pcb) to runnable queue at high priority. sched_block internally always chooses the highest priority process available to resume.
void sched_resume(struct pcb *pcb) { proc_enqueue(&run_queue[0], pcb); }

// adds this process to sleep queue.
void sched_sleep(struct pcb *pcb)
{
    proc_enqueue(&sleep_queue, pcb);
}
// Called when current process (old) can no longer run (yielded)
void sched_block(struct pcb *old)
{
    uint64_t curr_time = (mtime_get() * 1000000000ULL) / time_base;

    struct pcb *prev = sleep_queue;
    // prev is the last element of the sleep_queue
    if (sleep_queue != NULL)
    {
        struct pcb *curr = sleep_queue->next;
        do
        {
            // Currently sleeping and deadline is over.
            if (curr->sleeping && curr->sleep_deadline <= curr_time)
            {
                // need to wake up curr
                curr->sleeping = 0;

                // remove curr from sleep queue
                prev->next = curr->next;

                // sleep_queue points to tail of list.
                // if removing tail, then result in null queue if only curr, or else prev (element before the old tail)
                if (curr == sleep_queue)
                {
                    sleep_queue = (curr->next == curr) ? NULL : prev;
                }
                // we move curr pointer forward, and make another var storing curr called curr_wake, so that we can wake it up.
                struct pcb *curr_wake = curr;
                curr = curr->next;

                // AI Used to understand where to use sched_resume.
                //  Put curr_wake back on run queue, it is no longer curr because we just moved curr to next
                sched_resume(curr_wake);
            }
            else
            {
                // prev & curr pointers are one in front one behind, to keep track of last value for sleep_queue.
                prev = curr;
                curr = curr->next;
            }

        } while (sleep_queue != NULL && curr != sleep_queue->next);
    }

    // choose next runnable process
    int p = 0; // first find highest priority process
    while (p < N_PRIORITIES && run_queue[p] == 0)
        p++;
    struct pcb *new = proc_dequeue(&run_queue[p]);

    // if we aren't changing processes, don't do anything.
    if (new != old)
    { // switch needed, basically just let the new process run.

        // hart number since process should run on same as old
        new->hart = old->hart;
        new->hart->needs_tlb_flush = 1;
        sched_set_self(new);
        L3(L_FREQ, L_CTX_SWITCH, (uintptr_t)old, (uintptr_t)new, new->hart->id);
        ctx_switch(&old->sp, new->sp);
        reap_zombies();
    }
}

// for starting a new user process.
void sched_run(int executable, struct rect area, void *args, int size)
{
    struct pcb *old = sched_self();
    proc_enqueue(&run_queue[0], old);
    struct pcb *new = proc_create(old->hart, executable, area, args, size);
    sched_set_self(new);
    L4(L_NORM, L_CTX_START, (uintptr_t)old, (uintptr_t)new, new->hart->id, executable);
    ctx_start(&old->sp, (char *)new + PAGE_SIZE);
    reap_zombies();
}

// running process can invoke to give up CPU and let somebody else run.
void sched_yield(void)
{
    struct pcb *current = sched_self();
    if (current->executable > 0)
    { // idle loop doesn't yield at interrupts
        proc_enqueue(&run_queue[1], current);
        sched_block(current);
    }
}
