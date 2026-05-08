#include "embryos.h"

enum { V, R, W, X, U, G, A, D };
#define PTE(x)              ((uword_t) 1 << (x))
#define PT_ENTRY(pa, flags) ((((uword_t)(pa) & ~ (uword_t) 0xFFF) >> 2) | (flags))
#define PTE_COUNT           (PAGE_SIZE / sizeof(uword_t))
#define BPW                 (8 * sizeof(uword_t))   // bits per word
#define RW                  (PTE(V)|PTE(R)|PTE(W)|PTE(A)|PTE(D))
#define RWX                 (RW|PTE(X))

#if VBITS == 39

// Extracts VPN[1] from [29:21]. Extracts VPN[0] from [20:12]. 
// If vbit of idx1 is 0, then page is unused. memsets new empty non-leaf page L2.
// Writes leaf PTE into l2[idx0] that user mode can touch (U).
void vm_map(void *base, uintptr_t va, void *frame) {
    uword_t *l1 = base;
    int idx1 = (va >> 21) & (PTE_COUNT - 1);
    uword_t *l2;
    if (!(l1[idx1] & PTE(V))) {
        l2 = frame_alloc();
        memset(l2, 0, PAGE_SIZE);
        l1[idx1] = PT_ENTRY((uintptr_t) l2, PTE(V));
    } else {
        l2 = (uword_t *)((uintptr_t)(l1[idx1] >> 10) << 12);
    }
    int idx0 = (va >> 12) & (PTE_COUNT - 1);
    l2[idx0] = PT_ENTRY((uintptr_t) frame, RWX|PTE(U));
}

// Same traversal, but ultimately checks if L2 table exists, and if so check if L2 table has actually been mapped
int vm_is_mapped(void *base, uintptr_t va) {
    uword_t *l1 = base;
    int idx1 = (va >> 21) & (PTE_COUNT - 1);
    if (!(l1[idx1] & PTE(V))) return 0;
    uword_t *l2 = (uword_t *)((uintptr_t)(l1[idx1] >> 10) << 12);
    int idx0 = (va >> 12) & (PTE_COUNT - 1);
    return l2[idx0] & PTE(V);
}

void vm_flush(struct hart *hart, void *base) {
    hart->root_page_table[VM_START >> 30] = PT_ENTRY((uintptr_t) base, PTE(V));
    tlb_flush();
}
// Walk whole tree, gives every allocated frame back. For each of 512 slots in base, walk L2 it points to. 
// Free each valid leaf's physical frame then L2 page table itself.
void vm_release(void *base) {
    uword_t *l1 = base;
    for (int i = 0; i < PTE_COUNT; i++) {
        uword_t pte = l1[i];
        if (!(pte & PTE(V))) continue;
        uword_t *l2 = (uword_t *)((uintptr_t)(pte >> 10) << 12);
        for (int j = 0; j < PTE_COUNT; j++) {
            uword_t leaf = l2[j];
            if (leaf & PTE(V))
                frame_release((void *)((uintptr_t)(leaf >> 10) << 12));
        }
        frame_release(l2);
    }
}
// process frees base (pointer to 4KB page where each entry is PTE either to nothing or to a L2 page table) afterwards in process.c

// Already correct -- already has two levels 
// a root_pt (L1) that maps 1GB chunks, and parent_page_table (L2) for the VM_START region
void vm_init(struct hart *hart) {
    uword_t *root_pt = frame_alloc();
    hart->root_page_table = root_pt; 
    hart->parent_page_table = frame_alloc();
    memset(hart->parent_page_table, 0, PAGE_SIZE);
    // First map everything 1-1
    for (int i = 0; i < PTE_COUNT; i++)
        root_pt[i] = PT_ENTRY(i * 0x40000000ULL, RWX|PTE(G));

    // Update the entry of VM_START
    root_pt[VM_START >> 30] =           // 12 + 9 + 9
        PT_ENTRY((uword_t) hart->parent_page_table, PTE(V));

    if (hart->idx == 0) kprintf("Enabling virtual memory now\n");
    vm_enable(((uword_t) 1 << (BPW - 1)) | (((uword_t) root_pt) >> 12));
    tlb_flush();
    if (hart->idx == 0) kprintf("Virtual memory enabled\n");
}

#endif // VBITS == 39
