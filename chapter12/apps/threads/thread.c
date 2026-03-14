#include "../syslib.h"
#include "thread.h"
// to access ctx start and switch.
#include "../shared/ctx.h"
#include <stdlib.h>
#include "../malloc.h"

// Contains possible states of thread.
typedef enum
{
    THREAD_RUNNABLE,
    THREAD_SLEEPING,
    THREAD_WAITING_INPUT,
    THREAD_WAITING_SEMA,
} thread_state_t;

// Struct for a thread.
struct thread
{
    void *sp;                // saved stack pointer (used by ctx_switch)
    void *stack_base;        // pointer to malloc'd stack (for free() on exit)
    unsigned int stack_size; // size of the stack allocation

    thread_state_t state; // current blocking state

    uint64_t wake_time; // for THREAD_SLEEPING: deadline in ns
    int input_result;   // for THREAD_WAITING_INPUT: delivered value. Stores what key is pressed, like to move a player in game.c.

    struct sema *waiting_on; // for THREAD_WAITING_SEMA: which semaphore

    struct thread *next; // what is next, for the corresponding queue below of the thread

    void (*func)(void *); // the function the thread executes
    void *arg;            // the arguments to the function

    int started; // 0 if never run, 1 after first ctx_start. Tracks whether thread has ever been dispatched before and maybe has just been switched out of.
    // Logic: everytime a thread wants to know what key is pressed, it calls thread_get(), then it goes to the input queue and some other thread starts running,
    // then when the user presses a key, schedule internally uses user_get(0) to tell the thread and wake it back up with the information of what key was pressed.
};

struct thread *run_queue;        // list of RUNNABLE threads
struct thread *run_queue_tail;   // tail of run_queue
struct thread *sleep_queue;      // list of THREAD_SLEEPING threads (sorted by wake_time)
struct thread *sleep_queue_tail; // tail of sleep_queue
struct thread *input_queue;      // list of THREAD_WAITING_INPUT threads
struct thread *input_queue_tail; // tail of input_queue
struct thread *current_thread;   // the currently executing thread
struct thread *zombie_queue;     // list of exited threads waiting to be freed
struct thread *zombie_queue_tail;

void thread_create(void (*f)(void *), void *arg, unsigned int stack_size)
{
    // Step 1: Allocate a new TCB. I.e., make a thread struct tracking thread state.
    struct thread *tcb = malloc(sizeof(struct thread));

    // Step 2: Allocate a stack of at least stack_size bytes
    void *stack_base = malloc(stack_size);

    // Step 3: Store f, arg, stack_base, stack_size in the TCB

    tcb->stack_base = stack_base;
    tcb->stack_size = stack_size;
    tcb->func = f;
    tcb->arg = arg;

    // Step 4: Set up new stack so ctx_start can bootstrap
    tcb->sp = (void *)((char *)stack_base + stack_size);
    // set to top of stack (base + size) because stacks go downward

    // Define this thread as runnable, and append to run_queue.
    tcb->state = THREAD_RUNNABLE;
    tcb->started = 0;
    tcb->next = NULL;

    if (run_queue == NULL)
    {
        run_queue = tcb;
    }
    else
    {
        run_queue_tail->next = tcb;
    }
    run_queue_tail = tcb;
}

static void schedule(void)
{

    // Drain the zombie queue, but skip any entry that is still the current_thread
    // (a dying thread calls schedule() before switching away — freeing its own
    // stack before ctx_switch would be a use-after-free).
    struct thread *z = zombie_queue;
    zombie_queue = NULL;
    zombie_queue_tail = NULL;
    while (z != NULL)
    {
        struct thread *znext = z->next;
        if (z == current_thread)
        {
            // Still on this thread's stack — re-enqueue and free after switch.
            z->next = NULL;
            if (zombie_queue == NULL)
            {
                zombie_queue = z;
            }
            else
            {
                zombie_queue_tail->next = z;
            }
            zombie_queue_tail = z;
        }
        else
        {
            free(z->stack_base);
            free(z);
        }
        z = znext;
    }

    // Check the sleep queue — iterate sleep_queue, call user_gettime(), and move any thread whose wake_time has passed back to run_queue.
    struct thread *prev = NULL;
    struct thread *sleeping = sleep_queue;
    while (sleeping != NULL)
    {
        struct thread *next = sleeping->next;
        if (user_gettime() >= sleeping->wake_time)
        {
            // Remove from sleep_queue
            if (prev == NULL)
            {
                sleep_queue = next;
            }
            else
            {
                prev->next = next;
            }
            if (sleeping == sleep_queue_tail)
            {
                sleep_queue_tail = prev;
            }
            // Append to run_queue
            sleeping->state = THREAD_RUNNABLE;
            sleeping->next = NULL;
            if (run_queue == NULL)
            {
                run_queue = sleeping;
            }
            else
            {
                run_queue_tail->next = sleeping;
            }
            run_queue_tail = sleeping;
        }
        else
        {
            prev = sleeping;
        }
        sleeping = next;
    }

    // Deliver one keypress to the head of the input queue.
    // If nothing else is runnable, block with user_get(1); otherwise poll with user_get(0).
    if (input_queue != NULL)
    {

        // Case 1: run queue is null. no threads can run, so we must wait for a key (user_get(1)) blocks till arrives
        // Otherwise, other threads are ready so we peek without waiting, and return -1 if nothing.
        // If any key came in, we wake the first input-waiting thread. Otherwise this block is skipped and we just run a runnable thread instead in the next block.
        int result = (run_queue == NULL && sleep_queue == NULL) ? user_get(1) : user_get(0);
        // if (result != -1)
        // {
        struct thread *t = input_queue;
        input_queue = t->next;
        if (input_queue == NULL)
            input_queue_tail = NULL;
        t->input_result = result;
        t->state = THREAD_RUNNABLE;
        t->next = NULL;
        if (run_queue == NULL)
        {
            run_queue = t;
        }
        else
        {
            run_queue_tail->next = t;
        }
        run_queue_tail = t;
        // }
    }

    // Dispatch: pop the head of run_queue and switch to it
    struct thread *to_run = run_queue;
    if (to_run == NULL)
    {
        // Nothing runnable. If nothing is sleeping or waiting for input either, we're done.
        if (sleep_queue == NULL && input_queue == NULL)
        {
            user_exit();
        }
        // Only sleeping threads remain — block until the earliest deadline,
        // then loop back through schedule() to wake them.
        if (sleep_queue != NULL)
        {
            user_sleep(sleep_queue->wake_time);
        }
        schedule();
        return;
    }

    run_queue = to_run->next;
    if (run_queue == NULL)
        run_queue_tail = NULL;
    to_run->next = NULL;

    struct thread *old = current_thread;
    current_thread = to_run;

    // This actually starts or wakes back up the thread. Needs the old stack pointer and the new stack pointer.
    // If old == to_run we are already on this thread (e.g. the only thread got its own keypress back);
    // skip the switch entirely so we don't restore from uninitialized stack memory.
    if (old == to_run)
    {
        return;
    }
    else if (to_run->started == 0)
    {
        to_run->started = 1;
        // ctx_start saves old->sp (main TCB's sp is fine to clobber if it's exiting)
        ctx_start(&old->sp, to_run->sp);
    }
    else
    {
        // has already been started now just waking it up.
        ctx_switch(&old->sp, to_run->sp);
    }
}

// initializes thread subsystem and a calling thread (first one).
void thread_init()
{
    run_queue = NULL;
    run_queue_tail = NULL;
    sleep_queue = NULL;
    sleep_queue_tail = NULL;
    input_queue = NULL;
    input_queue_tail = NULL;
    zombie_queue = NULL;
    zombie_queue_tail = NULL;

    // Per the slides: allocate a TCB for the calling (main) thread.
    // The process already has a stack, so stack_base stays NULL (never freed).
    struct thread *main_tcb = malloc(sizeof(struct thread));
    main_tcb->stack_base = NULL;
    main_tcb->stack_size = 0;
    main_tcb->state = THREAD_RUNNABLE;
    main_tcb->started = 1; // already running
    main_tcb->next = NULL;
    main_tcb->func = NULL;
    main_tcb->arg = NULL;
    current_thread = main_tcb;
}

// Current thread yields. Remove from runnable. schedule() called to decide who goes next (the new current_thread)
void thread_yield()
{
    current_thread->state = THREAD_RUNNABLE;
    current_thread->next = NULL;
    if (run_queue == NULL)
    {
        run_queue = current_thread;
    }
    else
    {
        run_queue_tail->next = current_thread;
    }
    run_queue_tail = current_thread;

    schedule();
}

// Logic for the current thread to sleep.
//  ns here is the deadline in nanoseconds since boot time, till which point the calling thread is suspended.
void thread_sleep(uint64_t ns)
{
    current_thread->state = THREAD_SLEEPING;
    // avoid doublecount, seems to pass in user_gettime() in game.c anyway.
    current_thread->wake_time = ns;

    // INVARIANT -> SLEEP QUEUE SORTED BY DEADLINE
    // Insert current_thread into sleep_queue sorted by wake_time
    struct thread *prev = NULL;
    struct thread *cur = sleep_queue;
    while (cur != NULL && cur->wake_time <= current_thread->wake_time)
    {
        prev = cur;
        cur = cur->next;
    }
    current_thread->next = cur;
    if (prev == NULL)
    {
        sleep_queue = current_thread;
    }
    else
    {
        prev->next = current_thread;
    }
    if (cur == NULL)
    {
        sleep_queue_tail = current_thread;
    }

    schedule();
}

// For threads who are blocked waiting for a keypress, put in input_queue
int thread_get()
{
    current_thread->state = THREAD_WAITING_INPUT;
    current_thread->next = NULL;
    if (input_queue == NULL)
    {
        input_queue = current_thread;
    }
    else
    {
        input_queue_tail->next = current_thread;
    }
    input_queue_tail = current_thread;

    schedule();

    return current_thread->input_result;
}

void thread_exit()
{
    // Per slides: if no other thread is runnable, end the process now.
    // We cannot block here — the dying thread has nowhere to switch to.
    if (run_queue == NULL)
    {
        user_exit();
    }
    // Enqueue on zombie queue; the next living thread's schedule() will free it.
    current_thread->next = NULL;
    if (zombie_queue == NULL)
    {
        zombie_queue = current_thread;
    }
    else
    {
        zombie_queue_tail->next = current_thread;
    }
    zombie_queue_tail = current_thread;
    schedule();
}

// Allocates new semaphore. count has initial value (# of concurrent holders). waiters is queue of threads blocked waiting for sema
struct sema *sema_create(unsigned int count)
{
    struct sema *s = malloc(sizeof(*s));
    s->count = count;
    s->waiters = NULL;
    return s;
}

// Note: doesn't call schedule() at end, because not blocking function
//(nobody needs to give up stuff or wait), just keeps going with moving waiter onto run queue.
void sema_inc(struct sema *sema)
{
    //     Wake exactly one blocked thread, if any
    // Otherwise increment the count
    if (sema->waiters != NULL)
    {
        struct thread *to_wake = sema->waiters;
        sema->waiters = to_wake->next;
        to_wake->next = NULL;
        // move to runnable, not waiting on any semas.
        to_wake->state = THREAD_RUNNABLE;
        to_wake->waiting_on = NULL;
        if (run_queue == NULL)
        {
            run_queue = to_wake;
        }
        else
        {
            run_queue_tail->next = to_wake;
        }
        run_queue_tail = to_wake;
    }
    else
    {
        // if there are no waiters, no one is currently blocked. Gives one more credit so that 1 person can access the lock.
        sema->count += 1;
    }
}

void sema_dec(struct sema *sema)
{
    if (sema->count > 0)
    {
        sema->count -= 1;
        return;
    }
    // count is how many threads can proceed past this point simultaneously.
    // This point -> 0 count. this thread is not allowed to proceed, so is blocked by this sema.

    current_thread->state = THREAD_WAITING_SEMA;
    current_thread->waiting_on = sema; // current calling thread is blocked by this sema.
    current_thread->next = NULL;
    if (sema->waiters == NULL)
    {
        sema->waiters = current_thread;
    }
    else
    {
        struct thread *t = sema->waiters;
        // iterate till tail of sema waiters and append.
        while (t->next != NULL)
            t = t->next;
        t->next = current_thread;
    }
    schedule();
}

// only release semaphore after all queues are done with it.
void sema_release(struct sema *sema)
{
    free(sema);
}

// Called by ctx_start when bootstrapping a new thread.
// Runs the thread's function, then exits automatically.
void exec_user(void)
{
    current_thread->func(current_thread->arg);
    // after we've made it this far, no need to keep thread around -- it's done its purpose.
    // Calls the thread's actual function, then calls exit after function returns. need to clear garbage.
    thread_exit();
}
