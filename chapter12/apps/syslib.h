#pragma once

#include "syscall.h"
#include "platform.h"

extern void user_exit();
extern void user_yield();
extern void user_spawn(int app, int row, char col, int wd, int ht, const void *args, int sz);
extern void user_put(int row, int col, cell_t cell);
extern int user_get(int block);
extern int user_create(void);
extern int user_read(int file, int off, void *dst, int n);
extern int user_write(int file, int off, const void *src, int n);
extern int user_size(int file);
extern void user_delete(int file);
extern uint64_t user_gettime(void);
extern void user_sleep(uint64_t deadline);

static inline void user_delay(int ms)
{ // pseudo system call
    if (--ms <= 0)
    {
        return;
    }
    user_sleep(user_gettime() + (ms * 1000000));
    return;
}
