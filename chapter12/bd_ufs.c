#include "bd_ufs.h"
#include <string.h>
#include <stddef.h>

#define UFS_NIND UFS_PTRS_PER_BLOCK

/* ------------------------------
 * Low-level block helpers
 * ------------------------------ */

static void ufs_read_block(struct ufs_state *s, int blk, void *dst)
{
  s->lower->read(s->lower->state, s->inode_below, blk, dst);
}

static void ufs_write_block(struct ufs_state *s, int blk, const void *src)
{
  s->lower->write(s->lower->state, s->inode_below, blk, src);
}

static void ufs_read_super(struct ufs_state *s, struct ufs_superblock *sb)
{
  ufs_read_block(s, 0, sb);
}

static void ufs_write_super(struct ufs_state *s, const struct ufs_superblock *sb)
{
  ufs_write_block(s, 0, sb);
}

/* ------------------------------
 * Inode helpers
 * ------------------------------ */

static void ufs_inode_loc(struct ufs_state *s, int ino_num, int *blk_no, int *idx)
{
  *blk_no = 1 + (ino_num / UFS_INODES_PER_BLOCK);
  *idx = ino_num % UFS_INODES_PER_BLOCK;
}

static void ufs_read_inode(struct ufs_state *s, int ino_num, struct ufs_inode *ino)
{
  int blk, idx;
  ufs_inode_loc(s, ino_num, &blk, &idx);
  struct block b;
  ufs_read_block(s, blk, &b);
  struct ufs_inode *arr = (struct ufs_inode *)b.bytes;
  *ino = arr[idx];
}

static void ufs_write_inode(struct ufs_state *s, int ino_num, const struct ufs_inode *ino)
{
  int blk, idx;
  ufs_inode_loc(s, ino_num, &blk, &idx);
  struct block b;
  ufs_read_block(s, blk, &b);
  struct ufs_inode *arr = (struct ufs_inode *)b.bytes;
  arr[idx] = *ino;
  ufs_write_block(s, blk, &b);
}

/* ------------------------------
 * Free-list management
 * ------------------------------ */

int ufs_alloc_block(struct ufs_state *s)
{
  struct ufs_superblock sb;
  ufs_read_super(s, &sb);
  uint32_t head = sb.free_list_head;
  if (!head)
    return -1;

  struct ufs_ptr_block fb;
  ufs_read_block(s, head, &fb);

  // Try to take a block from entries [1..]
  for (int i = 1; i < UFS_NIND; i++)
  {
    if (fb.ptrs[i])
    {
      uint32_t b = fb.ptrs[i];
      fb.ptrs[i] = 0;
      ufs_write_block(s, head, &fb);
      return (int)b;
    }
  }

  // No free entries; move to next free-list block
  uint32_t next = fb.ptrs[0];
  sb.free_list_head = next;
  ufs_write_super(s, &sb);

  if (!next)
    return -1;
  return ufs_alloc_block(s); // recurse to use new head
}

void ufs_free_block(struct ufs_state *s, int bno)
{
  if (bno <= 0)
    return; // never free superblock or invalid

  // Never free inode blocks
  if (bno < 1 + s->n_inode_blocks)
    return;

  struct ufs_superblock sb;
  ufs_read_super(s, &sb);

  if (sb.free_list_head == 0)
  {
    // Make bno the first free-list block
    struct ufs_ptr_block fb;
    memset(&fb, 0, sizeof fb);
    fb.ptrs[0] = 0;
    ufs_write_block(s, bno, &fb);
    sb.free_list_head = (uint32_t)bno;
    ufs_write_super(s, &sb);
    return;
  }

  struct ufs_ptr_block head;
  ufs_read_block(s, sb.free_list_head, &head);
  for (int i = 1; i < UFS_NIND; i++)
  {
    if (head.ptrs[i] == 0)
    {
      head.ptrs[i] = (uint32_t)bno;
      ufs_write_block(s, sb.free_list_head, &head);
      return;
    }
  }

  // Head full: make bno a new free-list block
  struct ufs_ptr_block fb;
  memset(&fb, 0, sizeof fb);
  fb.ptrs[0] = sb.free_list_head;
  ufs_write_block(s, bno, &fb);
  sb.free_list_head = (uint32_t)bno;
  ufs_write_super(s, &sb);
}

/* ------------------------------
 * Block mapping
 * ------------------------------ */

static uint32_t ufs_inode_get_block(struct ufs_state *s, struct ufs_inode *ino, int blk_index)
{
  (void)s;

  if (blk_index == 0)
    return ino->direct;

  blk_index--;

  if (blk_index < UFS_NIND)
  {
    if (!ino->indirect)
      return 0;
    struct ufs_ptr_block ib;
    ufs_read_block(s, ino->indirect, &ib);
    return ib.ptrs[blk_index];
  }

  blk_index -= UFS_NIND;
  if (!ino->double_indirect)
    return 0;

  int outer = blk_index / UFS_NIND;
  int inner = blk_index % UFS_NIND;

  struct ufs_ptr_block dib;
  ufs_read_block(s, ino->double_indirect, &dib);
  uint32_t ind_blk = dib.ptrs[outer];
  if (!ind_blk)
    return 0;

  struct ufs_ptr_block ib;
  ufs_read_block(s, ind_blk, &ib);
  return ib.ptrs[inner];
}

static uint32_t ufs_inode_get_or_alloc_block(struct ufs_state *s, struct ufs_inode *ino, int blk_index)
{
  // Direct
  if (blk_index == 0)
  {
    if (!ino->direct)
    {
      int b = ufs_alloc_block(s);
      if (b < 0)
        return 0;
      ino->direct = (uint32_t)b;
    }
    return ino->direct;
  }

  blk_index--;

  // Indirect region
  if (blk_index < UFS_NIND)
  {
    if (!ino->indirect)
    {
      int b = ufs_alloc_block(s);
      if (b < 0)
        return 0;
      ino->indirect = (uint32_t)b;
      struct ufs_ptr_block ib;
      memset(&ib, 0, sizeof ib);
      ufs_write_block(s, ino->indirect, &ib);
    }
    struct ufs_ptr_block ib;
    ufs_read_block(s, ino->indirect, &ib);
    if (!ib.ptrs[blk_index])
    {
      int b = ufs_alloc_block(s);
      if (b < 0)
        return 0;
      ib.ptrs[blk_index] = (uint32_t)b;
      ufs_write_block(s, ino->indirect, &ib);
    }
    return ib.ptrs[blk_index];
  }

  blk_index -= UFS_NIND;

  // Double-indirect region
  if (!ino->double_indirect)
  {
    int b = ufs_alloc_block(s);
    if (b < 0)
      return 0;
    ino->double_indirect = (uint32_t)b;
    struct ufs_ptr_block dib;
    memset(&dib, 0, sizeof dib);
    ufs_write_block(s, ino->double_indirect, &dib);
  }

  int outer = blk_index / UFS_NIND;
  int inner = blk_index % UFS_NIND;

  struct ufs_ptr_block dib;
  ufs_read_block(s, ino->double_indirect, &dib);
  if (!dib.ptrs[outer])
  {
    int b = ufs_alloc_block(s);
    if (b < 0)
      return 0;
    dib.ptrs[outer] = (uint32_t)b;
    struct ufs_ptr_block ib;
    memset(&ib, 0, sizeof ib);
    ufs_write_block(s, dib.ptrs[outer], &ib);
    ufs_write_block(s, ino->double_indirect, &dib);
  }

  struct ufs_ptr_block ib;
  ufs_read_block(s, dib.ptrs[outer], &ib);
  if (!ib.ptrs[inner])
  {
    int b = ufs_alloc_block(s);
    if (b < 0)
      return 0;
    ib.ptrs[inner] = (uint32_t)b;
    ufs_write_block(s, dib.ptrs[outer], &ib);
  }
  return ib.ptrs[inner];
}

/* ------------------------------
 * bd interface: alloc/free/size/read/write
 * ------------------------------ */

int ufs_alloc(void *st)
{
  struct ufs_state *s = st;
  int ino = 0;
  struct block b;

  for (int blk = 1; blk < 1 + s->n_inode_blocks; blk++)
  {
    ufs_read_block(s, blk, &b);
    struct ufs_inode *arr = (struct ufs_inode *)b.bytes;
    for (int i = 0; i < UFS_INODES_PER_BLOCK && ino < s->n_inodes; i++, ino++)
    {
      if (!arr[i].allocated)
      {
        memset(&arr[i], 0, sizeof arr[i]);
        arr[i].allocated = 1;
        ufs_write_block(s, blk, &b);
        return ino;
      }
    }
  }
  return -1;
}

void ufs_free(void *st, int inode)
{
  struct ufs_state *s = st;
  if (inode < 0 || inode >= s->n_inodes)
    return;

  struct ufs_inode ino;
  ufs_read_inode(s, inode, &ino);
  if (!ino.allocated)
    return;

  int max_blocks = 1 + UFS_NIND + UFS_NIND * UFS_NIND;

  // Free all data blocks reachable from this inode
  for (int i = 0; i < max_blocks; i++)
  {
    uint32_t bno = ufs_inode_get_block(s, &ino, i);
    if (bno)
    {
      ufs_free_block(s, (int)bno);
    }
  }

  // Free indirect and double-indirect blocks themselves
  if (ino.indirect)
  {
    ufs_free_block(s, (int)ino.indirect);
  }
  if (ino.double_indirect)
  {
    ufs_free_block(s, (int)ino.double_indirect);
  }

  memset(&ino, 0, sizeof ino);
  ufs_write_inode(s, inode, &ino);
}

int ufs_size(void *st, int inode)
{
  (void)st;
  (void)inode;
  return 1 + UFS_NIND + UFS_NIND * UFS_NIND;
}

void ufs_read(void *st, int inode, int blk, void *dst)
{
  struct ufs_state *s = st;
  if (inode < 0 || inode >= s->n_inodes)
  {
    memcpy(dst, &bd_null_block, sizeof(struct block));
    return;
  }

  struct ufs_inode ino;
  ufs_read_inode(s, inode, &ino);
  if (!ino.allocated)
  {
    memcpy(dst, &bd_null_block, sizeof(struct block));
    return;
  }

  uint32_t bno = ufs_inode_get_block(s, &ino, blk);
  if (!bno)
  {
    memcpy(dst, &bd_null_block, sizeof(struct block));
  }
  else
  {
    ufs_read_block(s, (int)bno, dst);
  }
}

void ufs_write(void *st, int inode, int blk, const void *src)
{
  struct ufs_state *s = st;
  if (inode < 0 || inode >= s->n_inodes)
    return;

  struct ufs_inode ino;
  ufs_read_inode(s, inode, &ino);
  if (!ino.allocated)
    return;

  uint32_t bno = ufs_inode_get_or_alloc_block(s, &ino, blk);
  if (!bno)
    return; // out of space

  // Persist any updated inode pointers
  ufs_write_inode(s, inode, &ino);
  ufs_write_block(s, (int)bno, src);
}

// Initialization

void ufs_init(struct bd *iface, struct ufs_state *s, struct bd *lower, int inode_below, int n_inodes)
{
  s->lower = lower;
  s->inode_below = inode_below;
  s->n_inodes = n_inodes;

  // Determine total blocks available in the lower inode
  int total_blocks = lower->size(lower->state, inode_below);

  // Compute number of inode blocks needed, ceiling division in case n_inodes does not divide evenly into
  s->n_inode_blocks =
      (n_inodes + UFS_INODES_PER_BLOCK - 1) / UFS_INODES_PER_BLOCK;

  if (1 + s->n_inode_blocks >= total_blocks)
  {
    // case if there is not enough space for superblock + all inode blocks
    s->n_inode_blocks = total_blocks > 1 ? total_blocks - 1 : 0;
  }

  // Format superblock
  struct ufs_superblock sb;
  memset(&sb, 0, sizeof sb);
  sb.n_inode_blocks = (uint32_t)s->n_inode_blocks;

  int first_data = 1 + s->n_inode_blocks;
  if (first_data >= total_blocks)
  {
    sb.free_list_head = 0;
    ufs_write_super(s, &sb);
    return;
  }

  // Build free list over [first_data .. total_blocks-1]
  int current_fl_block = -1;
  int current_fl_index = 1;

  for (int b = first_data; b < total_blocks; b++)
  {
    if (current_fl_block == -1)
    {
      // Start a new free-list block at b
      current_fl_block = b;
      current_fl_index = 1;

      struct ufs_ptr_block fb;
      memset(&fb, 0, sizeof fb);
      fb.ptrs[0] = sb.free_list_head; // previous head
      ufs_write_block(s, current_fl_block, &fb);

      sb.free_list_head = (uint32_t)current_fl_block;
    }
    else
    {
      struct ufs_ptr_block fb;
      ufs_read_block(s, current_fl_block, &fb);
      fb.ptrs[current_fl_index++] = (uint32_t)b;
      ufs_write_block(s, current_fl_block, &fb);

      if (current_fl_index >= UFS_NIND)
      {
        current_fl_block = -1;
      }
    }
  }

  ufs_write_super(s, &sb);

  // Zero out the inode blocks
  struct block zero;
  memset(&zero, 0, sizeof zero);
  for (int blk = 1; blk < first_data; blk++)
  {
    ufs_write_block(s, blk, &zero);
  }

  // Hook up bd interface
  iface->state = s;
  iface->alloc = ufs_alloc;
  iface->free = ufs_free;
  iface->size = ufs_size;
  iface->read = ufs_read;
  iface->write = ufs_write;
}
