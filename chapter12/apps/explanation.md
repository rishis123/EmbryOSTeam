Threading Project explanation:

Thread control block design

- Thread control blocks are designed as structs, encompassing all the fields that would be necessary for a thread.
- We have sp, the stack pointer of the current thread, that is used by ctx_switch and ctx_start. This houses all the relevant registers for this thread, so switching/startign is simply a matter of reading off the new registers from the stack.
- We have stack_base, which also points to the stack, but sp points to where the program is currently executing while the stack_base points to the original pointer location on the stack. This is necessary when it comes time to free a thread.
- We have stack_size, which is just atop stack_base and is how much space each thread is allowed to use in its stack.
- We have state, which is the current state of the thread -- a runnable (either currently executing or in a runnable queue), sleeping (waiting for some future deadline), waiting input (waiting for some keystroke), and waiting for a semaphore.
- We have wake_time, which for sleeping threads represents what time the thread should wake up at, in ns from boot time
- We have input_result, which is a private keyboard entry for each thread, to avoid one keystroke activating all threads.
- We have waiting_on, which is a pointer to the semaphore that the current thread is waiting for.
- We have next, which is a next pointer, for whichever one of the queues (see below) that the current thread is on.
- We have func and arg, which is the actual logic that this thread wants to do.
- Finally, started tracks whether this thread needs to be started or switched into.

Run-queue organization

- Instead of just a run queue and zombie queue from the slides, we have 4 queues. run_queue for runnables (ready to go), sleep_queue for waiting for a deadline, input_queue for waiting for a keystroke, and zombie_queue for threads that need to be freed. This is beneficial because it allows for separation of different thread reasons, and avoids having to check at each thread in a master run_queue is waiting for a deadline or a keystroke or something else. The run_queue has a head and tail pointer, and uses that structure to help with appending and checking for NULL cases. 


Input multiplexing approach
- input_queue manages multiplexing, with the schedule function orchestrating keystrokes for the current thread and blocking if waiting. That way, a thread isn't just spinning till it receives input but gives up control to a thread that is able to run while it is waiting for a keystroke.


Semaphore blocking and wake-up logic
- Semaphores are defined as a struct with a count of the number of threads that can access it now, and a waiters list. Thus, each semaphore has its own queue of threads, and sema_inc unblocks one thread and increases the count accordingly, while sema_dec does the opposite. Thus, the application code can use these two as lock and unlocks and readily access the waiters. 


AI Statement:

Used Claude Sonnet to develop pseudocode roadmap for how to implement functions. Used Claude Code to better understand what functions do, for some of the in line comments, and for code logic help, especially with schedule.


Instructions on how to play the game:
Catch balls falling from the top of the screen with your basket (represented as a bar) before they reach the bottom of the screen.
Press 'a' on the keyboard to move your basket left and 'd' to move your basket right. If the ball lands into the basket, you get a
point, otherwise the computer gets a point. Press 'q' to exit the game.









# User-Level Threads, Blocking I/O, and Synchronization

## Overview

In this project, you will implement a **user-level threading library** that supports:

* Cooperative multithreading
* Blocking sleep
* Blocking input
* Counting semaphores

All threads run within a **single EmbryOS user process**. The kernel is unaware of threads and schedules only the process as a whole. All thread scheduling, blocking, and synchronization happen entirely in **user space**.

In addition to the library itself, you will build an **animated application** that demonstrates concurrency and blocking input without freezing animation.

---

## Threading Model

* Threads are **cooperatively scheduled**
* There is **no preemption**
* A thread runs until it:

  * calls `thread_yield()`
  * calls `thread_sleep()`
  * blocks in `thread_get()`
  * blocks in `sema_dec()`
  * exits
* Context switches are explicit and use provided assembly helpers

---

## Required API

You must implement the following interface:

```c
void thread_init();
void thread_create(void (*f)(void *), void *arg, unsigned int stack_size);
void thread_yield();
void thread_sleep(uint64_t ns);
int  thread_get();
void thread_exit();

struct sema *sema_create(unsigned int count);
void sema_inc(struct sema *sema);
void sema_dec(struct sema *sema);
void sema_release(struct sema *sema);
```

---

## Program Structure Requirements

* `main()` **must call** `thread_init()` first, become the first thread
* `main()` **must call** `thread_exit()` last, but this should not terminate the program if other threads are still running
* Any other thread whose function returns must **automatically call**
  `thread_exit()` (so all but the first thread do not have to call
  `thread_exit` explicitly)
* `thread_exit()` should never return
* When the last thread exits, or when all remaining threads are blocked
  on semaphore, the program should terminate.
* As long as there are threads running or waiting for input, the program
  should not terminate.

---

## Context Switching Primitives

You must use the following functions from `ctx.h`:

```c
extern void ctx_switch(void **old_sp, void *new_sp);
extern void ctx_start(void **save_sp, void *new_sp);
```

### Semantics

* **`ctx_switch()`**

  * Saves the caller’s registers on its stack
  * Stores the caller’s stack pointer in `*old_sp`
  * Switches to `new_sp`
  * Restores registers and resumes execution

* **`ctx_start()`**

  * Used to start a brand-new thread
  * The new stack contains no saved registers
  * Begins execution by calling `exec_user()`

---

## Thread Lifecycle

### Thread Creation

```c
thread_create(f, arg, stack_size);
```

* Allocates a stack of at least `stack_size` bytes, aligned at 16 bytes)
    (using `malloc`, which already aligns at 16 bytes)

---

### Thread Termination

```c
void thread_exit();
```

* Removes the calling thread from scheduling
* Eventually frees associated resources (stack, control block)
* Switches to another runnable thread
* Recall that a thread cannot clean itself up (with no stack, the code would not be able to call `ctx_switch`)

---

## Blocking Sleep

```c
void thread_sleep(uint64_t deadline);
```

* Suspends the calling thread until deadline expires
  (where deadline is the time in nanoseconds since boot time)
* Must **not** busy wait

---

## Blocking Input: `thread_get()`

### Semantics

```c
int thread_get();
```

`thread_get()` behaves **exactly like `user_get(1)`**, but at the **thread level**.

It:

* **Blocks the calling thread**
* Returns one of:

  * A character
  * `USER_GET_GOT_FOCUS`
  * `USER_GET_LOST_FOCUS`

There is **no non-blocking variant**.

---

### Correct Behavior

* If input is already available, return immediately
* Otherwise:

  * Block the calling thread
  * Yield to another runnable thread
* When input arrives:

  * Wake **exactly one** thread waiting for input
  * Deliver the input event to that thread
* Input events must **not be lost**

## Scheduler Requirements

Your scheduler must support blocking for **four distinct reasons**:

1. Runnable
2. Sleeping (`thread_sleep`)
3. Waiting for input (`thread_get`)
4. Waiting on a semaphore (`sema_dec`)

Blocked threads must never appear in the runnable queue until their blocking condition is resolved.

---

## Semaphores

You must implement **counting semaphores**.

### Creation

```c
struct sema *sema_create(unsigned int count);
```

---

### Operations

```c
void sema_dec(struct sema *sema);   // P(rocure)
void sema_inc(struct sema *sema);   // V(acate)
void sema_release(struct sema *sema);
```

#### Semantics

* `sema_dec()`:

  * If count > 0, decrement and return
  * If count == 0, block the calling thread

* `sema_inc()`:

  * Wake **exactly one** blocked thread, if any
  * Otherwise increment the count

* `sema_release()`:

  * Frees the semaphore

---

## Animated Demo Application

You must build an animated application that:

* Creates multiple threads
* Uses `thread_sleep()` for timing
* Uses `thread_get()` for input
* Uses semaphores for synchronization
* Continues animating **while input is blocked**

Examples:

* Moving objects
* Bouncing shapes
* Progress bars
* Multiple independently animated elements

The demo must clearly show that:

* Blocking input does **not** freeze the application
* Threads resume correctly after input arrives

---

## Deliverables

```
threads/
├── README.md        # Design explanation
├── thread.c         # Threading implementation
├── thread.h         # Interface
└── (optional files)
```

Your README must briefly explain:

* Thread control block design
* Run-queue organization
* Input multiplexing approach
* Semaphore blocking and wake-up logic

---

## Constraints

* No kernel modifications
* All context switching must use `ctx_switch()` / `ctx_start()`

---

## Summary

This project implements a **complete user-level runtime system**:

* Cooperative threads
* Blocking sleep
* Blocking I/O
* Synchronization primitives

Correctness depends on careful scheduling, clean separation of concerns, and precise handling of blocking conditions. This is one of the most conceptually important projects in EmbryOS—design it carefully before you code.
