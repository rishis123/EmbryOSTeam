#include "embryos.h"

/* ------------------------------
 * ufs alloc_block and free_block
 * ------------------------------ */

// allocates a single block
int ufs_alloc_block(struct ufs_state *s)
{
  struct ufs_superblock *sb = (struct ufs_superblock *)bd_alloc();
  s->lower->read(s->lower->state, s->inode_below, 0, sb);

  uint32_t head = sb->free_list_head;
  if (head == 0)
  {
    bd_free((struct block *)sb);
    die("disk full"); // all free list blocks are full
    return -1;
  }

  struct ufs_ptr_block *fb = (struct ufs_ptr_block *)bd_alloc(); // free list block struct
  s->lower->read(s->lower->state, s->inode_below, head, fb);

  while (head != 0)
  {
    s->lower->read(s->lower->state, s->inode_below, head, fb); // reload every iteration
    for (int i = 1; i < UFS_PTRS_PER_BLOCK; i++)
    {
      if (fb->ptrs[i] != 0)
      {
        uint32_t b = fb->ptrs[i];
        fb->ptrs[i] = 0;
        s->lower->write(s->lower->state, s->inode_below, head, fb);

        s->lower->write(s->lower->state, s->inode_below, b, &bd_null_block); // zero out the block b
        bd_free((struct block *)sb);
        bd_free((struct block *)fb);
        return (int)b;
      }
    }
    head = fb->ptrs[0];
  }

  bd_free((struct block *)sb);
  bd_free((struct block *)fb);

  die("disk full"); // all free list blocks are full
  return -1;
}

// frees a single block with identifier b
void ufs_free_block(struct ufs_state *s, int b)
{
  if (b < 1 + s->n_inode_blocks)
  {
    return;
  } // do nothing if block is out of bounds or trying to free inode/superblock

  struct ufs_superblock *sb = (struct ufs_superblock *)bd_alloc();
  s->lower->read(s->lower->state, s->inode_below, 0, sb);

  struct ufs_ptr_block *fb = (struct ufs_ptr_block *)bd_alloc(); // free list block struct

  if (sb->free_list_head == 0) // completely out of free list blocks so we turn the block b into a free list block
  {
    for (int i = 0; i < UFS_PTRS_PER_BLOCK; i++)
    {
      fb->ptrs[i] = 0; // free list block to set to all zeros, and ptrs[0] = next = 0 since only one free list block after this is done
    }
    s->lower->write(s->lower->state, s->inode_below, b, fb);
    sb->free_list_head = (uint32_t)b; // update value in superblock and write it down
    s->lower->write(s->lower->state, s->inode_below, 0, sb);
    bd_free((struct block *)sb);
    bd_free((struct block *)fb);
    return;
  }

  uint32_t head = sb->free_list_head;

  while (head != 0)
  {
    s->lower->read(s->lower->state, s->inode_below, head, fb);
    for (int i = 1; i < UFS_PTRS_PER_BLOCK; i++)
    {
      if (fb->ptrs[i] == 0)
      {
        fb->ptrs[i] = (uint32_t)b;
        s->lower->write(s->lower->state, s->inode_below, head, fb);
        bd_free((struct block *)sb);
        bd_free((struct block *)fb);
        return; // done since we wrote the block b into an element of the free list block array
      }
    }
    head = fb->ptrs[0];
  }

  // overflow case, make a completely new free list block and set it as the new head and connect to previous head
  for (int i = 1; i < UFS_PTRS_PER_BLOCK; i++)
  {
    fb->ptrs[i] = 0; // free list block to set to all zeros
  }
  fb->ptrs[0] = sb->free_list_head; // ptrs[0] = next = prev. free list head
  s->lower->write(s->lower->state, s->inode_below, b, fb);
  sb->free_list_head = (uint32_t)b;
  s->lower->write(s->lower->state, s->inode_below, 0, sb);
  bd_free((struct block *)sb);
  bd_free((struct block *)fb);
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
        b->inode_block[i].direct = 0;
        b->inode_block[i].indirect = 0;
        b->inode_block[i].double_indirect = 0; // zero out all fields of inode except allocated flag
        b->inode_block[i].allocated = 1;       // now the inode is allocated
        s->lower->write(s->lower->state, s->inode_below, blk, b);
        bd_free((struct block *)b);
        return inode;
      }
    }
  }
  bd_free((struct block *)b);
  return -1;
}

void ufs_free(void *st, int inode)
{
  struct ufs_state *s = st;

  if (inode < 0 || inode >= s->n_inodes)
  {
    return;
  } // out of bounds

  int blk = 1 + (inode / UFS_INODES_PER_BLOCK);
  int idx = inode % UFS_INODES_PER_BLOCK;
  // get corresponding block and index in the block of the inode

  struct ufs_inode_block *b = (struct ufs_inode_block *)bd_alloc();
  s->lower->read(s->lower->state, s->inode_below, blk, b);

  struct ufs_inode ino = b->inode_block[idx];

  if (!ino.allocated)
  {
    bd_free((struct block *)b);
    return;
  } // not allocated so we are already done

  if (ino.direct != 0)
  {

    ufs_free_block(s, (int)ino.direct);
  }

  if (ino.indirect != 0)
  {
    struct ufs_ptr_block *ib = (struct ufs_ptr_block *)bd_alloc();
    s->lower->read(s->lower->state, s->inode_below, ino.indirect, ib);

    for (int i = 0; i < UFS_PTRS_PER_BLOCK; i++)
    {
      if (ib->ptrs[i] != 0)
      {
        ufs_free_block(s, (int)ib->ptrs[i]);
      }
    }
    // Finally free the indirect block itself
    ufs_free_block(s, (int)ino.indirect);
    bd_free((struct block *)ib);
  }

  if (ino.double_indirect != 0)
  {
    struct ufs_ptr_block *dib = (struct ufs_ptr_block *)bd_alloc();
    s->lower->read(s->lower->state, s->inode_below, ino.double_indirect, dib);

    for (int i = 0; i < UFS_PTRS_PER_BLOCK; i++)
    {
      if (dib->ptrs[i] != 0)
      {
        // Read the indirect block
        struct ufs_ptr_block *sib = (struct ufs_ptr_block *)bd_alloc();
        s->lower->read(s->lower->state, s->inode_below, dib->ptrs[i], sib);

        for (int j = 0; j < UFS_PTRS_PER_BLOCK; j++)
        {
          if (sib->ptrs[j] != 0)
          {
            ufs_free_block(s, (int)sib->ptrs[j]);
          }
        }
        // Free the indirect block
        ufs_free_block(s, (int)dib->ptrs[i]);
        bd_free((struct block *)sib);
      }
    }
    // free the double indirect block itself
    ufs_free_block(s, (int)ino.double_indirect);
    bd_free((struct block *)dib);
  }
  ino.allocated = 0;
  ino.direct = 0;
  ino.indirect = 0;
  ino.double_indirect = 0;

  b->inode_block[idx] = ino; // write the updated inode (allocated=0) back into the block buffer
  s->lower->write(s->lower->state, s->inode_below, blk, b);
  bd_free((struct block *)b); // free inode block
}

int ufs_size(void *st, int inode)
{
  return 1 + UFS_PTRS_PER_BLOCK + UFS_PTRS_PER_BLOCK * UFS_PTRS_PER_BLOCK;
  // 1 direct + (block size / 4 bytes per pointer) from indirect + (block size / 4 bytes per pointer) ^ 2 from double-indirect
}

// In effect, we check if the inode block is direct, indirect, or doubly-indirect (in terms of degrees of separation from data block). Then we read from it and copy into dst.
// blk == 0 → direct (inode holds the block number itself)
// 1 <= blk <= UFS_PTRS_PER_BLOCK → singly-indirect (inode → pointer block → data)
// blk > UFS_PTRS_PER_BLOCK → doubly-indirect (inode → pointer block → pointer block → data)
void ufs_read(void *st, int inode, int blk, void *dst)
{
  struct ufs_state *s = st;

  if (inode < 0 || inode >= s->n_inodes)
  {
    die("simple_read: bad offset");
  } // out of bounds

  // read inode block and idx
  int iblk = 1 + (inode / UFS_INODES_PER_BLOCK);
  int idx = inode % UFS_INODES_PER_BLOCK;

  // buffer to hold the raw inode block read from disk
  struct ufs_inode_block *iblock = (struct ufs_inode_block *)bd_alloc();
  // read the inode block containing our target inode from the lower layer
  s->lower->read(s->lower->state, s->inode_below, iblk, iblock);

  // extract the specific inode at position idx within the block
  struct ufs_inode ino = iblock->inode_block[idx];

  // if the inode is not allocated, treat the read as a hole and return zeros
  if (!ino.allocated)
  {
    memset(dst, 0, BLOCK_SIZE);
    bd_free((struct block *)iblock);
    return;
  }

  // variable for block number, will eventually hold the physical block number on the lower device
  uint32_t bno = 0;

  // scratch buffer for the indirect pointer block
  struct ufs_ptr_block *ib = (struct ufs_ptr_block *)bd_alloc();
  // scratch buffer for the double-indirect pointer block
  struct ufs_ptr_block *dib = (struct ufs_ptr_block *)bd_alloc();

  // block index 0 maps to the single direct pointer in the inode
  if (blk == 0)
  {
    // direct block: read straight from the inode's direct pointer
    bno = ino.direct;
  }
  else
  {
    // shift index down by 1 so bi=0 is the first indirect slot
    int bi = blk - 1;

    // check if bi falls within the singly-indirect range
    if (bi < UFS_PTRS_PER_BLOCK)
    {
      // only proceed if the indirect pointer block has been allocated
      if (ino.indirect)
      {
        // read the indirect pointer block from disk
        s->lower->read(s->lower->state, s->inode_below, ino.indirect, ib);
        // look up the physical block number at slot bi
        bno = ib->ptrs[bi];
      }
    }
    else
    {
      // bi is beyond the indirect range; shift into double-indirect range
      bi -= UFS_PTRS_PER_BLOCK;

      // only proceed if the double-indirect pointer block has been allocated
      if (ino.double_indirect)
      {
        // outer selects which singly-indirect block within the double-indirect block
        int outer = bi / UFS_PTRS_PER_BLOCK;
        // inner selects the slot within that singly-indirect block
        int inner = bi % UFS_PTRS_PER_BLOCK;

        // read the double-indirect pointer block
        s->lower->read(s->lower->state, s->inode_below, ino.double_indirect, dib);

        // fetch the physical block number of the relevant singly-indirect block
        uint32_t ind_blk = dib->ptrs[outer];
        // only proceed if that singly-indirect block has been allocated (non-zero = not a hole)
        if (ind_blk)
        {
          // read the singly-indirect pointer block from disk
          s->lower->read(s->lower->state, s->inode_below, ind_blk, ib);
          // look up the final physical block number at the inner slot
          bno = ib->ptrs[inner];
        }
      }
    }
  }

  // dst is where to put the output for requested block, if valid block number, we read in block into dst.
  //  if bno is still 0 the block was never written, i.e. hole, so return zeros
  if (!bno)
  {
    memset(dst, 0, BLOCK_SIZE);
  }
  else
  {
    // bno is valid: read the actual data block from the lower layer into dst
    s->lower->read(s->lower->state, s->inode_below, bno, dst);
  }
  bd_free((struct block *)ib);
  bd_free((struct block *)dib);
  bd_free((struct block *)iblock);
}

// Very similar to ufs_read logically (check direct/indirect/double indirect), but allocates block using ufs_allocate_block, zero intiializes and persists using block device write function (lower->write.)
void ufs_write(void *st, int inode, int blk, const void *src)
{
  struct ufs_state *s = st;

  if (inode < 0 || inode >= s->n_inodes)
  {
    return;
  }

  // get block and idx of inode
  int iblk = 1 + (inode / UFS_INODES_PER_BLOCK);
  int idx = inode % UFS_INODES_PER_BLOCK;

  // scratch buffer to hold the raw inode block read from disk
  struct ufs_inode_block *iblock = (struct ufs_inode_block *)bd_alloc();
  // read the inode block containing our target inode from the lower layer
  s->lower->read(s->lower->state, s->inode_below, iblk, iblock);

  // extract the specific inode at position idx within the block
  struct ufs_inode ino = iblock->inode_block[idx];

  if (!ino.allocated)
  {
    bd_free((struct block *)iblock);
    return;
  }

  uint32_t bno = 0;
  // scratch buffer for the indirect pointer block
  struct ufs_ptr_block *ib = (struct ufs_ptr_block *)bd_alloc();
  // scratch buffer for the double-indirect pointer block
  struct ufs_ptr_block *dib = (struct ufs_ptr_block *)bd_alloc();

  if (blk == 0) // direct
  {
    if (!ino.direct)
    {
      int b = ufs_alloc_block(s);
      if (b < 0)
      {
        bd_free((struct block *)ib);
        bd_free((struct block *)dib);
        bd_free((struct block *)iblock);
        return;
      }
      ino.direct = (uint32_t)b;
    }
    bno = ino.direct;
  }
  else // single indirect
  {
    int bi = blk - 1;

    if (bi < UFS_PTRS_PER_BLOCK)
    {
      if (!ino.indirect)
      {
        int b = ufs_alloc_block(s);
        if (b < 0)
        {
          bd_free((struct block *)ib);
          bd_free((struct block *)dib);
          bd_free((struct block *)iblock);
          return;
        }
        ino.indirect = (uint32_t)b;
        memset(ib, 0, sizeof *ib);
        s->lower->write(s->lower->state, s->inode_below, ino.indirect, ib);
      }

      s->lower->read(s->lower->state, s->inode_below, ino.indirect, ib);

      if (!ib->ptrs[bi])
      {
        int b = ufs_alloc_block(s);
        if (b < 0)
        {
          bd_free((struct block *)ib);
          bd_free((struct block *)dib);
          bd_free((struct block *)iblock);
          return;
        }
        ib->ptrs[bi] = (uint32_t)b;
        s->lower->write(s->lower->state, s->inode_below, ino.indirect, ib);
      }

      bno = ib->ptrs[bi];
    }
    else // double indirect
    {
      bi -= UFS_PTRS_PER_BLOCK;

      if (!ino.double_indirect)
      {
        int b = ufs_alloc_block(s);
        if (b < 0)
        {
          bd_free((struct block *)ib);
          bd_free((struct block *)dib);
          bd_free((struct block *)iblock);
          return;
        }
        ino.double_indirect = (uint32_t)b;

        memset(dib, 0, sizeof *dib);
        s->lower->write(s->lower->state, s->inode_below, ino.double_indirect, dib);
      }

      int outer = bi / UFS_PTRS_PER_BLOCK;
      int inner = bi % UFS_PTRS_PER_BLOCK;

      s->lower->read(s->lower->state, s->inode_below, ino.double_indirect, dib);

      if (!dib->ptrs[outer])
      {
        int b = ufs_alloc_block(s);
        if (b < 0)
        {
          bd_free((struct block *)ib);
          bd_free((struct block *)dib);
          bd_free((struct block *)iblock);
          return;
        }

        dib->ptrs[outer] = (uint32_t)b;

        memset(ib, 0, sizeof *ib);
        s->lower->write(s->lower->state, s->inode_below, dib->ptrs[outer], ib);
        s->lower->write(s->lower->state, s->inode_below, ino.double_indirect, dib);
      }

      s->lower->read(s->lower->state, s->inode_below, dib->ptrs[outer], ib);

      if (!ib->ptrs[inner])
      {
        int b = ufs_alloc_block(s);
        if (b < 0)
        {
          bd_free((struct block *)ib);
          bd_free((struct block *)dib);
          bd_free((struct block *)iblock);
          return;
        }

        ib->ptrs[inner] = (uint32_t)b;
        s->lower->write(s->lower->state, s->inode_below, dib->ptrs[outer], ib);
      }

      bno = ib->ptrs[inner];
    }
  }

  // write updated inode to lower layer
  iblock->inode_block[idx] = ino;
  s->lower->write(s->lower->state, s->inode_below, iblk, iblock);

  // write the data block
  s->lower->write(s->lower->state, s->inode_below, bno, src);
  bd_free((struct block *)ib);
  bd_free((struct block *)dib);
  bd_free((struct block *)iblock);
}

// init

void ufs_init(struct bd *iface, struct ufs_state *s, struct bd *lower, int inode_below, int n_inodes)
{
  s->lower = lower;
  s->inode_below = inode_below;
  s->n_inodes = n_inodes;

  int total_blocks = lower->size(lower->state, inode_below);

  s->n_inode_blocks = (n_inodes + UFS_INODES_PER_BLOCK - 1) / UFS_INODES_PER_BLOCK;
  // ceiling division so we allocate correct number of blocks

  if (1 + s->n_inode_blocks >= total_blocks)
    s->n_inode_blocks = total_blocks > 1 ? total_blocks - 1 : 0; // to handle overflow of inode blocks

  struct ufs_superblock *sb = (struct ufs_superblock *)bd_alloc();
  memset(sb, 0, sizeof *sb);
  sb->n_inode_blocks = (uint32_t)s->n_inode_blocks;

  int first_data_block = 1 + s->n_inode_blocks;

  if (first_data_block >= total_blocks)
  {
    sb->free_list_head = 0;
    s->lower->write(s->lower->state, s->inode_below, 0, sb);
    bd_free((struct block *)sb);
    return;
  } // if we have too many inode blocks + superblock that there are no remaining blocks left

  int current_fl_block = -1;
  int current_fl_index = 1;

  for (int b = first_data_block; b < total_blocks; b++)
  {
    struct ufs_ptr_block *fb = (struct ufs_ptr_block *)bd_alloc();
    if (current_fl_block == -1) // create first free list block
    {
      current_fl_block = b;
      current_fl_index = 1;

      memset(fb, 0, sizeof *fb);
      fb->ptrs[0] = sb->free_list_head;
      s->lower->write(s->lower->state, s->inode_below, current_fl_block, fb);

      sb->free_list_head = (uint32_t)current_fl_block;
    }
    else // adding blocks to the free list block
    {
      s->lower->read(s->lower->state, s->inode_below, current_fl_block, fb);
      fb->ptrs[current_fl_index++] = (uint32_t)b;
      s->lower->write(s->lower->state, s->inode_below, current_fl_block, fb);

      if (current_fl_index >= UFS_PTRS_PER_BLOCK)
        current_fl_block = -1;
    }
    bd_free((struct block *)fb);
  }

  s->lower->write(s->lower->state, s->inode_below, 0, sb); // write the superblock

  struct block *zero = bd_alloc();
  memset(zero, 0, BLOCK_SIZE);

  for (int blk = 1; blk < first_data_block; blk++)
  {
    s->lower->write(s->lower->state, s->inode_below, blk, zero); // zero out all inode blocks for safety
  }
  iface->state = s;
  iface->alloc = ufs_alloc;
  iface->free = ufs_free;
  iface->size = ufs_size;
  iface->read = ufs_read;
  iface->write = ufs_write;
  bd_free((struct block *)zero);
  bd_free((struct block *)sb);
}
