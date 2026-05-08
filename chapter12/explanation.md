For paging assignment (P5)

* Goal
We extended the 64-bit (RISC-V Sv39) virtual memory system from a single-level user page table to a two-level user page table, growing the per-process virtual address range at VM_START from 2 MB to 256 MB (VM_END is 0x80000000 in platform.h). The bulk of the work is in vm39.c (vm_map, vm_is_mapped, vm_release); we also made small structural changes to vm_init, vm_flush, and hart.h to host the per-process root at the correct level of the Sv39 walk (see "Where base lives" below). vm32.c and the kernel's 1-1 mapping of physical memory and devices are untouched.

* Architecture overview
The kernel still owns the top of the Sv39 walk: a per-hart Level 2 root (1 GB stride per entry, indexed by VPN[2]) maps all of physical memory and device space 1-1. Originally the root was a local variable `root_pt` inside vm_init; we now also store it on the hart as `hart->root_page_table` so that vm_flush can reach it (see hart.h:11 and vm39.c:67). One of the root's entries (the one for the GB containing VM_START) is overridden during vm_init to point to the per-hart `parent_page_table` (Level 1, 2 MB stride per entry); this is the table the original design used to host the per-process leaf. With the new two-level user table, vm_flush installs the per-process `base` directly into `root_page_table[VM_START >> 30]` (Level 2 slot 1), which makes `base` itself live at hardware Level 1 and gives us two real hardware levels (`base` plus the dynamically allocated L2 tables) under it. parent_page_table is still allocated and wired in by vm_init for the rest of the GB; the Level-2 entry for VM_START's GB is simply repointed to `base` whenever a process is scheduled.

* Where base lives, and why vm_flush had to move it up a level
For 2-level walking under `base` (VPN[1] then VPN[0], 9+9+12 bits = 1 GB capacity), `base` must sit at Sv39 Level 1 from the hardware's point of view. That requires it to be installed in a Level 2 entry. The original vm_flush installed `base` at `parent_page_table[(VM_START >> 21) & 0x1FF]`, i.e. into a Level 1 entry, which would have made `base` Level 0 (only 512 * 4 KB = 2 MB of user VA, no room to walk further). The new vm_flush writes `hart->root_page_table[VM_START >> 30] = PT_ENTRY(base, PTE(V))`, putting `base` directly under the satp root and giving us 256 MB. This is the minimum change needed for the new vm_map's `base[VPN[1]]` indexing to mean anything to the MMU.

* PTE format reminder
A PTE is a 64-bit word. Bits [63:10] hold the PPN (physical page number, i.e. the physical address shifted right by 12). Bits [9:0] hold flags: V (valid, bit 0), R/W/X (bits 1-3), U (user, bit 4), G (global, bit 5), A (accessed, bit 6), D (dirty, bit 7). A non-leaf PTE has V=1 and R=W=X=0 (the hardware keeps walking). A leaf PTE has V=1 and at least one of R/W/X set (the walk stops, this is the answer). The macro PT_ENTRY((uintptr_t)pa, flags) packs a physical address and a flag set into the PTE encoding.

* Virtual-address decomposition
For a virtual address `va` in the user range:
  - VPN[1] = (va >> 21) & 0x1FF  // bits [29:21], 9 bits, indexes the L1 table (`base`)
  - VPN[0] = (va >> 12) & 0x1FF  // bits [20:12], 9 bits, indexes the L2 table (allocated on demand)
  - offset = va & 0xFFF          // bits [11:0], byte offset inside the 4 KB frame
Each table holds PTE_COUNT = PAGE_SIZE / sizeof(uword_t) = 4096 / 8 = 512 entries, which is why the masks are `& 0x1FF` and the loops in vm_release run from 0 to 511.

* vm_map(base, va, frame)
1. Compute idx1 = VPN[1]. Look at l1[idx1] (where l1 is `base` viewed as the L1 table).
2. If l1[idx1] is invalid (V bit clear), allocate a fresh L2 table frame with frame_alloc, zero it with memset, and store a non-leaf PTE in l1[idx1] using PT_ENTRY(l2, PTE(V)) -- V only, no R/W/X, so the hardware treats it as a pointer to the next level.
3. If l1[idx1] was already valid, decode the existing L2 table address out of it: ((l1[idx1] >> 10) << 12) reverses the encoding to get the physical address.
4. Compute idx0 = VPN[0]. Install the leaf at l2[idx0] = PT_ENTRY(frame, RWX|PTE(U)). RWX is defined at the top of vm39.c as V|R|W|X|A|D; OR'ing in PTE(U) makes the page user-accessible. Setting A and D up front avoids a fault just to flip those bits.

* vm_is_mapped(base, va)
Mirrors vm_map without writing. Compute idx1 and idx0. If l1[idx1] is invalid, return 0 immediately. Otherwise decode l2 the same way and return the V bit of l2[idx0].

* vm_release(base)
Walk the whole tree to give every allocated frame back to the free list. For each of the 512 entries in `base`, if it is valid and non-leaf, decode its L2 table; iterate the 512 entries of the L2 table and frame_release each leaf's PPN-decoded frame; then frame_release the L2 table itself. We deliberately do NOT frame_release(base) here -- proc_release in process.c calls frame_release(pcb->base) immediately after vm_release, so freeing it here would double-free the L1 frame and corrupt the free list.

* What changed in vm_init, vm_flush, and hart.h
hart.h: added `uword_t *root_page_table;` to struct hart so the Sv39 Level 2 root can be reached from outside vm_init.
vm_init: same as before (allocate root_pt, allocate parent_page_table, fill root_pt with 1 GB-superpage 1-1 mappings, override the VM_START GB to point at parent_page_table, satp = root_pt) plus one new line: `hart->root_page_table = root_pt;` so vm_flush can find it.
vm_flush: now installs `base` at the Level 2 root slot for the user's GB (`hart->root_page_table[VM_START >> 30] = PT_ENTRY(base, PTE(V))`) instead of at a Level 1 slot of parent_page_table. parent_page_table is still allocated by vm_init but is effectively unused once a process is scheduled, because the Level 2 entry for that GB is now repointed at the per-process `base`.

* Net effect
Previously `base` held leaf PTEs directly (1 level, 512 * 4 KB = 2 MB user VA). Now `base` is the per-process Level 1 table holding non-leaf PTEs that point to dynamically allocated Level 0 ("L2") tables (2 levels, 512 * 512 * 4 KB = 1 GB capacity, of which the [VM_START, VM_END) window uses 256 MB). L2 tables are allocated lazily on first write to a 2 MB region, so a process pays only for what it touches.

* How we used AI
We used Claude Code (Opus) to read through the existing vm39.c, platform.h, hart.h, process.c, and apps.c; work out the Sv39 PTE encoding and the VPN[1]/VPN[0] decomposition; draft the two-level vm_map / vm_is_mapped / vm_release bodies; identify the double-free hazard between vm_release and proc_release in process.c (which is why vm_release does not free `base` itself); and reason through why `base` had to be installed at Level 2 (rather than the original Level 1 slot) for the 2-level user walk to actually give 256 MB. We reviewed and edited the result before committing.

-----------------------------------------


FOR FILE SYSTEM ASSIGNMENT.


* Superblock layout
The superblock is in block 0. The superblock struct has the same (byte) size as a normal block, which is 2048 bytes. The superblock has a uint32_t variable for the number of inode blocks and a uint32_t variable storing a pointer to the first free-list block. So that the superblock has a size of 2048 bytes, the rest of the superblock is an array of uint32_t variables (called padding) to pad the struct to the correct size of 2048 bytes, which is (BLOCK_SIZE / 4) - 2, as a uint32_t has a size of 4 bytes.
* i-Node structure 
An inode contains a total of 4 uint32_t variables for a total size of 16 bytes. One for the allocation flag (allocated or free), one for a pointer to first data block (direct block), one for a pointer to an indirect block, and one for a pointer to a double-indirect block. A null pointer represents a hole. Thus, as an inode is 16 bytes large, an inode block contains 2048/16 = 128 inodes.
*  Block allocation strategy
We allocate a block in the helper function ufs_alloc_block. In ufs_alloc_block, we look through the free list starting with the head, which we get from the superblock. If the head pointer in the superblock is zero/null, this means there are no free blocks and thus we die with the message that the disk is full. Otherwise, as the free list block is represented as an array of 32-bit block pointers, we check all elements except the first pointer in the free list block, as the first pointer is a pointer to the next free list block. So, if we find an non-zero/non-null element in the array, we take allocate that free block and return it. Otherwise, we go to the next element in the free list block linked list and repeat. If we go through the entire linked list and do not find a block to allocate, we again die with the message that the disk is full. In ufs_write, we check whether the block index falls in the direct, indirect, or double indirect sections of the inode, and then allocates the correct number of blocks depending on the depth, i.e. if the inode's pointer for direct is zero/null, we alloc a block and write the returned value (value of the block) of ufs_alloc_block to the inode's direct field and update the state of said block in the lower layer. If the block index falls in the indirect section, we allocate a block with ufs_alloc_block to hold the pointers if it does not already exist, then allocate the actual data block, and in the double indirect section, we allocate a double indirect block if missing and the specific indirect block if that is also missing, and then allocate the actual data block. 
* Free-list organization
As mentioned above, the superblock has a pointer to the head of the free-list block. The first element of a free-list block points to the next free-list block in the linked list, so we will never drop free-list blocks, and all other elements contain free blocks or zero/null so we do not corrupt blocks that are now not free blocks. When allocating a block, we traverse the linked list until we find a non zero/null block, which represents a free block. We return the block address and set the element in the array to zero. When freeing a block in ufs_free_block, we see if the superblock pointer to the head of the free-list block is zero/null. If it is, we turn the newly freed block into a free list block which has all of its array elements as zero. Otherwise, we check if there is a free list block in the linked list which has an element in its array that is zero (that is not also the first element of said array, which is a pointer to the next free list block). If we find this, we replace the zero with the block address b and write it to the lower layer and are done. Otherwise, all free list blocks are full of free blocks, so we create a completely new free list block (with all array values representing free block addresses as zero) as the new head of the free list block linked list and set the next pointer to the old head.
* How holes are represented and handled
A hole is represented as a null pointer, i.e. if the direct field of an inode is zero, then block 0 of that inode is a hole. If we are reading and detect a hole at any level, we stop and set the destination block to all zeros. When writing, if we detect a hole at any level, i.e. at the double-indirect or indirect level, we first allocate another block to plug the hole so that we do not lose the block we allocate if we attempt to read it later, as otherwise we would still encounter a null pointer when trying to read from that block.
* How you used AI, if at all
We used Claude Code for code comprehension, completion, and comments, as well as for generating a test suite in apps (ufs_test). We used Gemini for identifying and fixing corner cases, and for debugging and checking for memory leaks when using bd_alloc.
