struct sema {
    //count of n = the next n calls/accesses from threads can pass through without blocking. e..g., for mutex the max is 1.
    unsigned int   count;
    struct thread *waiters;   // queue of THREAD_WAITING_SEMA threads. if they don't get sema access right away they wait here till count is high enough.
};
void thread_init();
void thread_create(void (*f)(void *), void *arg, unsigned int stack_size);
void thread_yield();
void thread_sleep(uint64_t ns);
int thread_get();
void thread_exit();
struct sema *sema_create(unsigned int count);
void sema_inc(struct sema *sema);
void sema_dec(struct sema *sema);
void sema_release(struct sema *sema);