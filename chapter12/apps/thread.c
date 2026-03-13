#include "thread.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

enum thread_state
{
  THREAD_RUNNABLE,
  THREAD_SLEEPING,
  THREAD_WAIT_INPUT,
  THREAD_WAIT_SEMA,
  THREAD_ZOMBIE,
};

struct tcb
{
  void *sp;   // saved stack pointer
  void *base; // malloc'd stack base (NULL for main thread)
  enum thread_state state;

  uint64_t wakeup_time;
  struct sema *waiting_on;

  int has_input;
  int input_val;

  struct tcb *next; // for queues
};

// Globals
static struct tcb *current;
static struct tcb *run_queue_head;
static struct tcb *run_queue_tail;
static struct tcb *zombie_queue_head;
static struct tcb *zombie_queue_tail;

void thread_init()
{
  // Initialize queues
  run_queue_head = run_queue_tail = NULL;
  zombie_queue_head = zombie_queue_tail = NULL;

  // Allocate TCB for main thread (no stack allocation)
  struct tcb *t = malloc(sizeof(struct tcb));
  memset(t, 0, sizeof(*t));

  // Main thread already has a stack → mark no allocated stack
  t->base = NULL;

  // Main thread starts as runnable
  t->state = THREAD_RUNNABLE;
  t->next = NULL;

  // Set current thread
  current = t;

  // Do NOT enqueue main on run queue; it is already running
}

void thread_create(void (*f)(void *), void *arg, unsigned int stack_size)
{
  struct tcb *t = malloc(sizeof(struct tcb));
  memset(t, 0, sizeof(*t));
}