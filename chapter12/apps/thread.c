#include "syslib.h"
#include "thread.h"
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
    int          input_result;  // for THREAD_WAITING_INPUT: delivered value

    struct sema *waiting_on;    // for THREAD_WAITING_SEMA: which semaphore

    struct thread *next;        // what is next, for the corresponding queue below of the thread

    void        (*func)(void *);   // the function the thread executes
    void        *arg;              // the arguments to the function

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
    tcb->state = THREAD_RUNNABLE;
    tcb->next  = NULL;

    if (run_queue == NULL) {
        run_queue = tcb;
    } else {
        run_queue_tail->next = tcb;
    }
    run_queue_tail = tcb;

}




void thread_init() {

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