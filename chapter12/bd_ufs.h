#pragma once

#include "bd.h"
#include <stdint.h>

#define UFS_PTRS_PER_BLOCK (BLOCK_SIZE / 4)
#define UFS_INODES_PER_BLOCK (BLOCK_SIZE / (4 * sizeof(uint32_t)))

// Superblock (block 0)
struct ufs_superblock
{
  uint32_t n_inode_blocks;                  // number of inode blocks
  uint32_t free_list_head;                  // first free‑list block (0 = none)
  uint32_t padding[UFS_PTRS_PER_BLOCK - 2]; // to fit superblock into size of one block
};

// Inode (4 x 32‑bit words)
struct ufs_inode
{
  uint32_t allocated;       // 0 = free, 1 = allocated
  uint32_t direct;          // direct data block
  uint32_t indirect;        // indirect block
  uint32_t double_indirect; // double‑indirect block
};

// Indirect, double‑indirect, and free‑list blocks format
struct ufs_ptr_block
{
  uint32_t ptrs[UFS_PTRS_PER_BLOCK];
};

// In‑memory state

struct ufs_state
{
  struct bd *lower;   // underlying block device
  int inode_below;    // inode on lower device
  int n_inodes;       // total number of inodes
  int n_inode_blocks; // number of inode blocks
};

// bd alloc, size, read, write, free implementations

int ufs_alloc(void *st);
int ufs_size(void *st, int inode);
void ufs_read(void *st, int inode, int blk, void *dst);
void ufs_write(void *st, int inode, int blk, const void *src);
void ufs_free(void *st, int inode);

// Internal helper interfaces

int ufs_alloc_block(struct ufs_state *s);
void ufs_free_block(struct ufs_state *s, int b);

/* Init for ufs
 *
 * iface       — the bd interface to populate
 * s           — the UFS state storage
 * lower       — underlying block device
 * inode_below — inode on the lower device where UFS lives
 * n_inodes    — minimum number of inodes to allocate
 */
void ufs_init(struct bd *iface, struct ufs_state *s, struct bd *lower, int inode_below, int n_inodes);
