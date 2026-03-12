#include "syslib.h"
#include "thread.h"
// to access ctx start and switch.
#include "../shared/ctx.h"
#include <stddef.h> 


// Contains possible states of thread.
typedef enum {
    THREAD_RUNNABLE,
    THREAD_SLEEPING,
    THREAD_WAITING_INPUT,
    THREAD_WAITING_SEMA,
} thread_state_t;

//Struct for a thread.
struct thread {
    void        *sp;            // saved stack pointer (used by ctx_switch)
    void        *stack_base;    // pointer to malloc'd stack (for free() on exit)
    unsigned int stack_size;    // size of the stack allocation

    thread_state_t state;       // current blocking state

    uint64_t     wake_time;     // for THREAD_SLEEPING: deadline in ns
    int          input_result;  // for THREAD_WAITING_INPUT: delivered value. Stores what key is pressed, like to move a player in game.c. 

    struct sema *waiting_on;    // for THREAD_WAITING_SEMA: which semaphore

    struct thread *next;        // what is next, for the corresponding queue below of the thread

    void        (*func)(void *);   // the function the thread executes
    void        *arg;              // the arguments to the function

    int          started;          // 0 if never run, 1 after first ctx_start. Tracks whether thread has ever been dispatched before and maybe has just been switched out of.
    //Logic: everytime a thread wants to know what key is pressed, it calls thread_get(), then it goes to the input queue and some other thread starts running, 
    //then when the user presses a key, schedule internally uses user_get(0) to tell the thread and wake it back up with the information of what key was pressed.

};

struct thread *run_queue;         // list of RUNNABLE threads
struct thread *run_queue_tail;    // tail of run_queue
struct thread *sleep_queue;       // list of THREAD_SLEEPING threads (sorted by wake_time)
struct thread *sleep_queue_tail;  // tail of sleep_queue
struct thread *input_queue;       // list of THREAD_WAITING_INPUT threads
struct thread *input_queue_tail;  // tail of input_queue
struct thread *current_thread;    // the currently executing thread

void thread_create(void (*f)(void *), void *arg, unsigned int stack_size) {
    //Step 1: Allocate a new TCB. I.e., make a thread struct tracking thread state.
    struct thread *tcb = malloc(sizeof(struct thread));

    //Step 2: Allocate a stack of at least stack_size bytes
    void *stack_base = malloc(stack_size);

    // Step 3: Store f, arg, stack_base, stack_size in the TCB

    tcb->stack_base = stack_base;
    tcb->stack_size = stack_size;
    tcb->func = f;
    tcb->arg = arg;

    //Step 4: Set up new stack so ctx_start can bootstrap
    tcb->sp = (void *)((char *)stack_base + stack_size); // 
    //set to top of stack (base + size) because stacks go downward
    
    // Define this thread as runnable, and append to run_queue.
    tcb->state   = THREAD_RUNNABLE;
    tcb->started = 0;
    tcb->next    = NULL;

    if (run_queue == NULL) {
        run_queue = tcb;
    } else {
        run_queue_tail->next = tcb;
    }
    run_queue_tail = tcb;

}

static void schedule(void) {
    // Check the sleep queue — iterate sleep_queue, call user_gettime(), and move any thread whose wake_time has passed back to run_queue.
    struct thread *prev = NULL;
    struct thread *sleeping = sleep_queue;
    while (sleeping != NULL) {
        struct thread *next = sleeping->next;
        if (user_gettime() >= sleeping->wake_time) {
            // Remove from sleep_queue
            if (prev == NULL) {
                sleep_queue = next;
            } else {
                prev->next = next;
            }
            if (sleeping == sleep_queue_tail) {
                sleep_queue_tail = prev;
            }
            // Append to run_queue
            sleeping->state = THREAD_RUNNABLE;
            sleeping->next = NULL;
            if (run_queue == NULL) {
                run_queue = sleeping;
            } else {
                run_queue_tail->next = sleeping;
            }
            run_queue_tail = sleeping;
        } else {
            prev = sleeping;
        }
        sleeping = next;
    }


    //Logic for input_quue, moving inputs that aren't' blocked into runnable queue
    prev = NULL; 
    // move prev from earlier
    struct thread *input = input_queue;

    while (input != NULL) {
        struct thread *next = input->next;
        // see if blocked, -1 if not blocked. process keeps running since 0.
        int result = user_get(0);
        // not blocked case
        if (result != -1) {
            // Remove from input_queue
            if (prev == NULL) {
                input_queue = next;
            } else {
                prev->next = next;
            }
            if (input == input_queue_tail) {
                input_queue_tail = prev;
            }
            // Deliver the keypress so thread_get() can return it
            input->input_result = result;
            // Append to run_queue
            input->state = THREAD_RUNNABLE;
            input->next = NULL;
            if (run_queue == NULL) {
                run_queue = input;
            } else {
                run_queue_tail->next = input;
            }
            run_queue_tail = input;
        } else {
            // move both pointers along in else case.
            prev = input;
        }
        input = next;
    }

    // Dispatch: pop the head of run_queue and switch to it
    struct thread *to_run = run_queue;
    if (to_run == NULL) return;

    run_queue = to_run->next;
    if (run_queue == NULL) run_queue_tail = NULL;
    to_run->next = NULL;

    struct thread *old = current_thread;
    current_thread = to_run;

    // This actually starts or wakes back up the thread. Needs the old stack pointer and the new stack pointer.
    if (to_run->started == 0) {
        to_run->started = 1;
        if (old == NULL) {
            //The case fo the very first thread ever to start, just pass in the current thread = to run stack pointer for old & new 
            ctx_start(&current_thread->sp, to_run->sp);
        } else {
            //Starting a new thread but there were ones before it.
            ctx_start(&old->sp, to_run->sp);
        }
    } else {
        // has already been started now just waking it up.
        ctx_switch(&old->sp, to_run->sp);
    }
}



void thread_init() {
    run_queue        = NULL;
    run_queue_tail   = NULL;
    sleep_queue      = NULL;
    sleep_queue_tail = NULL;
    input_queue      = NULL;
    input_queue_tail = NULL;
    current_thread   = NULL;


}


void thread_yield();
void thread_sleep(uint64_t ns) {


}
int thread_get();
void thread_exit();
struct sema *sema_create(unsigned int count);
void sema_inc(struct sema *sema);
void sema_dec(struct sema *sema);
void sema_release(struct sema *sema);


void main(void)
{
}