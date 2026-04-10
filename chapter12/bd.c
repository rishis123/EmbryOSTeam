#include "embryos.h"

// Threads have only limited stack space, but the block device layers
// often need a temporary block variable.  Instead of allocating it
// on the stack, it's better to use this heap.

#define MAX_HEAP    16

//when the block is free, it is on the free list so has a next pointer that lives inside its storage (hence use of union).
//but when the block is not-free (allocated), it doens't need the pointer (not on free list) and writes real data into the 2048 bytes, overwriting where next used to be.
union bd_free_block { union bd_free_block *next; struct block block; };

//pointer to first free block. just use ->next to traverse.
static union bd_free_block *bd_free_list;
static struct block bd_heap[MAX_HEAP];
const struct block bd_null_block;

//
struct block *bd_alloc(void) {
    union bd_free_block *bf = bd_free_list;
    if (bf == 0) die("bd_alloc: out of blocks");
    bd_free_list = bf->next;
    return &bf->block;
}
//set the newly freed block to the head of the free list.
void bd_free(struct block *b) {
    union bd_free_block *bf = (union bd_free_block *) b;
    bf->next = bd_free_list;
    bd_free_list = bf;
}
//adds all 16 heap blocks to free list.
void bd_init(void) {
    for (int i = 0; i < MAX_HEAP; i++) bd_free(&bd_heap[i]);
}
