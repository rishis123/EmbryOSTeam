/*
 * ufs_test.c - Tests for the UFS block store (bd_ufs.c)
 *
 * Covers:
 *   - Direct block (blk 0)
 *   - First and last singly-indirect blocks (blk 1, blk UFS_PTRS_PER_BLOCK)
 *   - First double-indirect block (blk UFS_PTRS_PER_BLOCK + 1)
 *   - Hole reads (never-written block returns zeros)
 *   - Overwrite (second write to same block wins)
 *   - File isolation (two files don't share blocks)
 *   - Multi-tier: direct + indirect + double-indirect in the same file
 */

#include "syslib.h"
#include "stdio.h"
#include "string.h"
#include "bd.h"   /* BLOCK_SIZE */

/* UFS_PTRS_PER_BLOCK = BLOCK_SIZE / sizeof(uint32_t) = 2048 / 4 = 512 */
#define UFS_PTRS_PER_BLOCK (BLOCK_SIZE / 4)

/* Block-index boundaries */
#define BLK_DIRECT          0
#define BLK_FIRST_INDIRECT  1
#define BLK_LAST_INDIRECT   (UFS_PTRS_PER_BLOCK)       /* blk 512 */
#define BLK_FIRST_DINDIRECT (UFS_PTRS_PER_BLOCK + 1)   /* blk 513 */

static char write_buf[BLOCK_SIZE];
static char read_buf[BLOCK_SIZE];

static int passes = 0;
static int fails  = 0;

/* Fill write_buf with value v */
static void fill(char v) {
    for (int i = 0; i < BLOCK_SIZE; i++) write_buf[i] = v;
}

/* Check read_buf is entirely value v */
static int all_eq(char v) {
    for (int i = 0; i < BLOCK_SIZE; i++)
        if (read_buf[i] != v) return 0;
    return 1;
}

/* Check read_buf is entirely zero */
static int all_zero(void) {
    return all_eq(0);
}

static void check(const char *name, int ok) {
    if (ok) {
        printf("  PASS: %s\n", name);
        passes++;
    } else {
        printf("  FAIL: %s\n", name);
        fails++;
    }
}

/* Write one block at blk_idx, read it back, verify all bytes equal v. */
static void test_rw(int file, int blk_idx, char v, const char *name) {
    fill(v);
    user_write(file, blk_idx * BLOCK_SIZE, write_buf, BLOCK_SIZE);
    int n = user_read(file, blk_idx * BLOCK_SIZE, read_buf, BLOCK_SIZE);
    check(name, n == BLOCK_SIZE && all_eq(v));
}

void main(void) {
    int f, f2, n;

    printf("=== UFS Block Store Tests ===\n\n");

    /* ------------------------------------------------------------------
     * Test 1: Direct block (blk 0)
     * ------------------------------------------------------------------ */
    printf("Test 1: direct block write/read\n");
    f = user_create();
    test_rw(f, BLK_DIRECT, 'A', "direct block");
    user_delete(f);

    /* ------------------------------------------------------------------
     * Test 2: First singly-indirect block (blk 1)
     * ------------------------------------------------------------------ */
    printf("Test 2: first indirect block write/read\n");
    f = user_create();
    test_rw(f, BLK_FIRST_INDIRECT, 'B', "first indirect block");
    user_delete(f);

    /* ------------------------------------------------------------------
     * Test 3: Last singly-indirect block (blk 512)
     *   bi = 512 - 1 = 511, 511 < UFS_PTRS_PER_BLOCK → still singly-indirect
     * ------------------------------------------------------------------ */
    printf("Test 3: last indirect block write/read (boundary)\n");
    f = user_create();
    test_rw(f, BLK_LAST_INDIRECT, 'C', "last indirect block");
    user_delete(f);

    /* ------------------------------------------------------------------
     * Test 4: First double-indirect block (blk 513)
     *   bi = 513 - 1 = 512, 512 >= UFS_PTRS_PER_BLOCK → double-indirect
     *   outer = 512 / 512 = 1, inner = 512 % 512 = 0
     * ------------------------------------------------------------------ */
    printf("Test 4: first double-indirect block write/read (boundary)\n");
    f = user_create();
    test_rw(f, BLK_FIRST_DINDIRECT, 'D', "first double-indirect block");
    user_delete(f);

    /* ------------------------------------------------------------------
     * Test 5: Hole read — reading a block that was never written
     *   Unwritten blocks must return zeros (UFS represents them as holes).
     * ------------------------------------------------------------------ */
    printf("Test 5: hole read returns zeros\n");
    f = user_create();
    /* Read indirect slot 1 without ever writing it */
    n = user_read(f, BLK_FIRST_INDIRECT * BLOCK_SIZE, read_buf, BLOCK_SIZE);
    check("unwritten indirect block is zero", all_zero());
    /* Also check direct slot */
    n = user_read(f, BLK_DIRECT * BLOCK_SIZE, read_buf, BLOCK_SIZE);
    check("unwritten direct block is zero", all_zero());
    user_delete(f);

    /* ------------------------------------------------------------------
     * Test 6: Overwrite — second write to the same block wins
     * ------------------------------------------------------------------ */
    printf("Test 6: overwrite same block\n");
    f = user_create();
    fill('E');
    user_write(f, BLK_DIRECT * BLOCK_SIZE, write_buf, BLOCK_SIZE);
    fill('F');
    user_write(f, BLK_DIRECT * BLOCK_SIZE, write_buf, BLOCK_SIZE);
    n = user_read(f, BLK_DIRECT * BLOCK_SIZE, read_buf, BLOCK_SIZE);
    check("overwrite: second write wins", n == BLOCK_SIZE && all_eq('F'));
    user_delete(f);

    /* ------------------------------------------------------------------
     * Test 7: File isolation — two files don't share blocks
     * ------------------------------------------------------------------ */
    printf("Test 7: two files are independent\n");
    fill('G');
    f  = user_create();
    user_write(f, BLK_DIRECT * BLOCK_SIZE, write_buf, BLOCK_SIZE);

    fill('H');
    f2 = user_create();
    user_write(f2, BLK_DIRECT * BLOCK_SIZE, write_buf, BLOCK_SIZE);

    n = user_read(f, BLK_DIRECT * BLOCK_SIZE, read_buf, BLOCK_SIZE);
    check("isolation: f1 block unaffected by f2 write", n == BLOCK_SIZE && all_eq('G'));

    n = user_read(f2, BLK_DIRECT * BLOCK_SIZE, read_buf, BLOCK_SIZE);
    check("isolation: f2 has its own data", n == BLOCK_SIZE && all_eq('H'));

    user_delete(f);
    user_delete(f2);

    /* ------------------------------------------------------------------
     * Test 8: Multi-tier — direct + indirect + double-indirect, same file
     *   Also verifies that an unwritten indirect slot within the same file
     *   is still a hole (zero), even after neighbouring slots are written.
     * ------------------------------------------------------------------ */
    printf("Test 8: direct + indirect + double-indirect in same file\n");
    f = user_create();

    fill('X');
    user_write(f, BLK_DIRECT       * BLOCK_SIZE, write_buf, BLOCK_SIZE);
    fill('Y');
    user_write(f, BLK_FIRST_INDIRECT * BLOCK_SIZE, write_buf, BLOCK_SIZE);
    fill('Z');
    user_write(f, BLK_FIRST_DINDIRECT * BLOCK_SIZE, write_buf, BLOCK_SIZE);

    n = user_read(f, BLK_DIRECT * BLOCK_SIZE, read_buf, BLOCK_SIZE);
    check("multi-tier: direct block correct", n == BLOCK_SIZE && all_eq('X'));

    n = user_read(f, BLK_FIRST_INDIRECT * BLOCK_SIZE, read_buf, BLOCK_SIZE);
    check("multi-tier: indirect block correct", n == BLOCK_SIZE && all_eq('Y'));

    n = user_read(f, BLK_FIRST_DINDIRECT * BLOCK_SIZE, read_buf, BLOCK_SIZE);
    check("multi-tier: double-indirect block correct", n == BLOCK_SIZE && all_eq('Z'));

    /* blk 2 was never written — must read as zero even though blk 1 was */
    n = user_read(f, 2 * BLOCK_SIZE, read_buf, BLOCK_SIZE);
    check("multi-tier: unwritten slot between written blocks is zero", all_zero());

    user_delete(f);

    /* ------------------------------------------------------------------
     * Summary
     * ------------------------------------------------------------------ */
    printf("\n=== Results: %d passed, %d failed ===\n", passes, fails);
}
