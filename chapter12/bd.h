#pragma once

//number of bytes per block.
#define BLOCK_SIZE  2048

//raw chunk of data (2048 bytes). Atomic unit of storage for OS, all files gets broken down and stored in blocks.
struct block { char bytes[BLOCK_SIZE]; };

// Block device interface.  Each block device has a device-specific
// state and a set of interface functions.  Abstractly, each block
// device offers 1 or more 'inodes', each of which is a fixed-size
// array of null-initialized blocks.
struct bd {
    //file system foundations. File system asks for a specific file (like a .txt in some folder), and block device translates to specific inode/block. 
    //Hides how data is physically stored. inodes are file containers (fixed-size array of blocks).


    void *state;     // depends on implementation of this interface
    int  (*alloc)(void *state);                 // allocate an inode
    int  (*size)(void *state, int inode);       // maximum size in blocks
    void (*read)(void *state, int inode, int blk, void *dst);
    void (*write)(void *state, int inode, int blk, const void *src);
    void (*free)(void *state, int inode);       // free an inode
};

//default block, all 0
extern const struct block bd_null_block;

//functions to allocate/free/init struct block.
struct block *bd_alloc(void);
void bd_free(struct block *b);
void bd_init(void);
