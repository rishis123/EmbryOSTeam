#include "bd_ufs.h"
#include <string.h>
#include <stddef.h>

/* ------------------------------
 * ufs alloc_block and free_block
 * ------------------------------ */

// allocates a single block
int ufs_alloc_block(struct ufs_state *s)
{
  struct ufs_superblock sb;
  s->lower->read(s->lower->state, s->inode_below, 0, &sb);

  uint32_t head = sb.free_list_head;

  struct ufs_ptr_block fb; // free list block struct
  s->lower->read(s->lower->state, s->inode_below, head, &fb);

  while (head != NULL)
  {
    for (int i = 1; i < UFS_PTRS_PER_BLOCK; i++)
    {
      if (fb.ptrs[i] != NULL) // found a free block somewhere in [1,N]
      {
        uint32_t b = fb.ptrs[i];
        fb.ptrs[i] = NULL;
        s->lower->write(s->lower->state, s->inode_below, head, &fb); // write updated fb back to lower
        return (int)b;
      }
    }
    head = fb.ptrs[0];
  }

  die("disk full"); // all free list blocks are full
}

// frees a single block with identifier b
void ufs_free_block(struct ufs_state *s, int b)
{
  if (b < 1 + s->n_inode_blocks)
  {
    return;
  } // do nothing if block is out of bounds or trying to free inode/superblock

  struct ufs_superblock sb;
  s->lower->read(s->lower->state, s->inode_below, 0, &sb);

  if (sb.free_list_head == NULL) // completely out of free list blocks so we turn the block b into a free list block
  {
    struct ufs_ptr_block fb; // free list block to set to all zeros
    memset(&fb, 0, sizeof fb);
    fb.ptrs[0] = NULL; // as next = NULL since only one free list block after this is done
    s->lower->write(s->lower->state, s->inode_below, b, &fb);
    sb.free_list_head = (uint32_t)b; // update value in superblock and write it down
    s->lower->write(s->lower->state, s->inode_below, 0, &sb);
    return;
  }

  uint32_t head = sb.free_list_head;

  struct ufs_ptr_block fb; // get the head free list block from lower
  s->lower->read(s->lower->state, s->inode_below, head, &fb);

  while (head != NULL)
  {
    for (int i = 1; i < UFS_PTRS_PER_BLOCK; i++)
    {
      if (fb.ptrs[i] == NULL)
      {
        fb.ptrs[i] = (uint32_t)b;
        s->lower->write(s->lower->state, s->inode_below, head, &fb);
        return; // done since we wrote the block b into an element of the free list block array
      }
    }
    head = fb.ptrs[0];
  }

  // overflow case, make a completely new free list block and set it as the new head and connect to previous head
  struct ufs_ptr_block fb;
  memset(&fb, 0, sizeof fb);
  fb.ptrs[0] = sb.free_list_head;
  s->lower->write(s->lower->state, s->inode_below, b, &fb);
  sb.free_list_head = (uint32_t)b;
  s->lower->write(s->lower->state, s->inode_below, 0, &sb);
}

// bd interface methods

int ufs_alloc(void *st)
{
  struct ufs_state *s = st;
  int inode = 0;
  struct ufs_inode_block *b = (struct ufs_inode_block *)bd_alloc();

  // loop through all inode blocks
  for (int blk = 1; blk < 1 + s->n_inode_blocks; blk++)
  {
    s->lower->read(s->lower->state, s->inode_below, blk, b);

    // loop through all inodes in the block, need to check if inode < n_inodes as may not be the case that every inode block is full
    for (int i = 0; i < UFS_INODES_PER_BLOCK && inode < s->n_inodes; i++, inode++)
    {
      if (!(b->inode_block[i].allocated))
      {
        memset(&b->inode_block[i], 0, sizeof b->inode_block[i]); // zero out the inode
        b->inode_block[i].allocated = 1;                         // now the inode is allocated
        s->lower->write(s->lower->state, s->inode_below, blk, b);
        return inode;
      }
    }
  }

  return 0;
}

void ufs_free(void *st, int inode)
{
  struct ufs_state *s = st;

  if (inode < 0 || inode >= s->n_inodes)
  {
    return;
  } // out of bounds

  int blk, idx;
  blk = 1 + (inode / UFS_INODES_PER_BLOCK);
  idx = inode % UFS_INODES_PER_BLOCK;
  // get corresponding block and index in the block of the inode

  struct ufs_inode_block *b = (struct ufs_inode_block *)bd_alloc();
  s->lower->read(s->lower->state, s->inode_below, blk, b);

  struct ufs_inode ino = b->inode_block[idx];

  if (!ino.allocated)
  {
    return;
  } // not allocated so we are already done

  ino.allocated = 0; // set flag to unallocated

  if (ino.direct != NULL)
  {
    ufs_free_block(s, (int)ino.direct);
  }

  if (ino.indirect != NULL)
  {
    ufs_free_block(s, (int)ino.indirect);
  } // this needs to be fixed along with double indiret to properly access the arrays

  if (ino.double_indirect != NULL)
  {
    ufs_free_block(s, (int)ino.double_indirect);
  }

  s->lower->write(s->lower->state, s->inode_below, blk, &b);
}

int ufs_size(void *st, int inode)
{
  return 1 + UFS_PTRS_PER_BLOCK + UFS_PTRS_PER_BLOCK * UFS_PTRS_PER_BLOCK;
  // 1 direct + (block size / 4 bytes per pointer) from indirect + (block size / 4 bytes per pointer) ** 2 from double-indirect
}

void ufs_read(void *st, int inode, int blk, void *dst)
{
  struct ufs_state *s = st;

  if (inode < 0 || inode >= s->n_inodes)
  {
    die("simple_read: bad offset");
  } // out of bounds

  // read inode
  int iblk = 1 + (inode / UFS_INODES_PER_BLOCK);
  int idx = inode % UFS_INODES_PER_BLOCK;

  struct block iblock;
  s->lower->read(s->lower->state, s->inode_below, iblk, &iblock);

  struct ufs_inode *arr = (struct ufs_inode *)iblock.bytes;
  struct ufs_inode ino = arr[idx];

  if (!ino.allocated)
  {
    memcpy(dst, &bd_null_block, sizeof(struct block));
    return;
  }

  /* Resolve block number */
  uint32_t bno = 0;

  if (blk == 0)
  {
    bno = ino.direct;
  }
  else
  {
    int bi = blk - 1;

    if (bi < UFS_PTRS_PER_BLOCK)
    {
      if (ino.indirect)
      {
        struct ufs_ptr_block ib;
        s->lower->read(s->lower->state, s->inode_below, ino.indirect, &ib);
        bno = ib.ptrs[bi];
      }
    }
    else
    {
      bi -= UFS_PTRS_PER_BLOCK;

      if (ino.double_indirect)
      {
        int outer = bi / UFS_PTRS_PER_BLOCK;
        int inner = bi % UFS_PTRS_PER_BLOCK;

        struct ufs_ptr_block dib;
        s->lower->read(s->lower->state, s->inode_below, ino.double_indirect, &dib);

        uint32_t ind_blk = dib.ptrs[outer];
        if (ind_blk)
        {
          struct ufs_ptr_block ib;
          s->lower->read(s->lower->state, s->inode_below, ind_blk, &ib);
          bno = ib.ptrs[inner];
        }
      }
    }
  }

  if (!bno)
  {
    memcpy(dst, &bd_null_block, sizeof(struct block));
  }
  else
  {
    s->lower->read(s->lower->state, s->inode_below, bno, dst);
  }
}

void ufs_write(void *st, int inode, int blk, const void *src)
{
  struct ufs_state *s = st;

  if (inode < 0 || inode >= s->n_inodes)
    return;

  /* Read inode */
  int iblk = 1 + (inode / UFS_INODES_PER_BLOCK);
  int idx = inode % UFS_INODES_PER_BLOCK;

  struct block iblock;
  s->lower->read(s->lower->state, s->inode_below, iblk, &iblock);

  struct ufs_inode *arr = (struct ufs_inode *)iblock.bytes;
  struct ufs_inode ino = arr[idx];

  if (!ino.allocated)
    return;

  /* Allocate or resolve block */
  uint32_t bno = 0;

  if (blk == 0)
  {
    if (!ino.direct)
    {
      int b = ufs_alloc_block(s);
      if (b < 0)
        return;
      ino.direct = (uint32_t)b;
    }
    bno = ino.direct;
  }
  else
  {
    int bi = blk - 1;

    if (bi < UFS_PTRS_PER_BLOCK)
    {
      if (!ino.indirect)
      {
        int b = ufs_alloc_block(s);
        if (b < 0)
          return;
        ino.indirect = (uint32_t)b;

        struct ufs_ptr_block ib;
        memset(&ib, 0, sizeof ib);
        s->lower->write(s->lower->state, s->inode_below, ino.indirect, &ib);
      }

      struct ufs_ptr_block ib;
      s->lower->read(s->lower->state, s->inode_below, ino.indirect, &ib);

      if (!ib.ptrs[bi])
      {
        int b = ufs_alloc_block(s);
        if (b < 0)
          return;
        ib.ptrs[bi] = (uint32_t)b;
        s->lower->write(s->lower->state, s->inode_below, ino.indirect, &ib);
      }

      bno = ib.ptrs[bi];
    }
    else
    {
      bi -= UFS_PTRS_PER_BLOCK;

      if (!ino.double_indirect)
      {
        int b = ufs_alloc_block(s);
        if (b < 0)
          return;
        ino.double_indirect = (uint32_t)b;

        struct ufs_ptr_block dib;
        memset(&dib, 0, sizeof dib);
        s->lower->write(s->lower->state, s->inode_below, ino.double_indirect, &dib);
      }

      int outer = bi / UFS_PTRS_PER_BLOCK;
      int inner = bi % UFS_PTRS_PER_BLOCK;

      struct ufs_ptr_block dib;
      s->lower->read(s->lower->state, s->inode_below, ino.double_indirect, &dib);

      if (!dib.ptrs[outer])
      {
        int b = ufs_alloc_block(s);
        if (b < 0)
          return;

        dib.ptrs[outer] = (uint32_t)b;

        struct ufs_ptr_block ib;
        memset(&ib, 0, sizeof ib);
        s->lower->write(s->lower->state, s->inode_below, dib.ptrs[outer], &ib);
        s->lower->write(s->lower->state, s->inode_below, ino.double_indirect, &dib);
      }

      struct ufs_ptr_block ib;
      s->lower->read(s->lower->state, s->inode_below, dib.ptrs[outer], &ib);

      if (!ib.ptrs[inner])
      {
        int b = ufs_alloc_block(s);
        if (b < 0)
          return;

        ib.ptrs[inner] = (uint32_t)b;
        s->lower->write(s->lower->state, s->inode_below, dib.ptrs[outer], &ib);
      }

      bno = ib.ptrs[inner];
    }
  }

  /* Persist updated inode */
  arr[idx] = ino;
  s->lower->write(s->lower->state, s->inode_below, iblk, &iblock);

  /* Write data block */
  s->lower->write(s->lower->state, s->inode_below, bno, src);
}

/* ------------------------------
 * Initialization
 * ------------------------------ */

void ufs_init(struct bd *iface,
              struct ufs_state *s,
              struct bd *lower,
              int inode_below,
              int n_inodes)
{
  s->lower = lower;
  s->inode_below = inode_below;
  s->n_inodes = n_inodes;

  int total_blocks = lower->size(lower->state, inode_below);

  s->n_inode_blocks = (n_inodes + UFS_INODES_PER_BLOCK - 1) / UFS_INODES_PER_BLOCK;
  // ceiling division so we allocate correct number of blocks

  if (1 + s->n_inode_blocks >= total_blocks)
    s->n_inode_blocks = total_blocks > 1 ? total_blocks - 1 : 0; // to handle overflow of inode blocks

  struct ufs_superblock sb;
  memset(&sb, 0, sizeof sb);
  sb.n_inode_blocks = (uint32_t)s->n_inode_blocks;

  int first_data_block = 1 + s->n_inode_blocks;

  if (first_data_block >= total_blocks)
  {
    sb.free_list_head = 0;
    s->lower->write(s->lower->state, s->inode_below, 0, &sb);
    return;
  } // if we have too many inode blocks + superblock that there are no remaining blocks left

  int current_fl_block = -1;
  int current_fl_index = 1;

  for (int b = first_data_block; b < total_blocks; b++)
  {
    if (current_fl_block == -1)
    {
      current_fl_block = b;
      current_fl_index = 1;

      struct ufs_ptr_block fb;
      memset(&fb, 0, sizeof fb);
      fb.ptrs[0] = sb.free_list_head;
      s->lower->write(s->lower->state, s->inode_below, current_fl_block, &fb);

      sb.free_list_head = (uint32_t)current_fl_block;
    }
    else
    {
      struct ufs_ptr_block fb;
      s->lower->read(s->lower->state, s->inode_below, current_fl_block, &fb);
      fb.ptrs[current_fl_index++] = (uint32_t)b;
      s->lower->write(s->lower->state, s->inode_below, current_fl_block, &fb);

      if (current_fl_index >= UFS_PTRS_PER_BLOCK)
        current_fl_block = -1;
    }
  }

  s->lower->write(s->lower->state, s->inode_below, 0, &sb);

  struct block zero;
  memset(&zero, 0, sizeof zero);

  for (int blk = 1; blk < first_data_block; blk++)
    s->lower->write(s->lower->state, s->inode_below, blk, &zero);

  iface->state = s;
  iface->alloc = ufs_alloc;
  iface->free = ufs_free;
  iface->size = ufs_size;
  iface->read = ufs_read;
  iface->write = ufs_write;
}
