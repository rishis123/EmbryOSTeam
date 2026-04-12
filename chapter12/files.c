#include "embryos.h"
#include "dir.h"

extern struct block ramdisk[], __ramdisk_end[];

struct bd ramdisk_iface;
struct ramdisk_state ramdisk_state;
// struct bd simple_iface;
// struct simple_state simple_state;
struct bd ufs_iface;
struct ufs_state ufs_state;
struct flat flat_fs;
void files_init(void)
{
    bd_init();
    ramdisk_init(&ramdisk_iface, &ramdisk_state,
                 ramdisk, __ramdisk_end - ramdisk);

    int n_files = 0;
    while (embedded_files[n_files].name != 0) n_files++;
    
    kprintf("n_files=%d\n", n_files);
    ufs_init(&ufs_iface, &ufs_state, &ramdisk_iface, 0, n_files + 3);
    kprintf("ufs_init done\n");

    // kprintf("ufs_init done\n");
    // int t0 = ufs_iface.alloc(ufs_iface.state);
    // int t1 = ufs_iface.alloc(ufs_iface.state);
    // int t2 = ufs_iface.alloc(ufs_iface.state);
    // kprintf("test allocs: %d %d %d\n", t0, t1, t2);

    flat_init(&flat_fs, &ufs_iface, 1);  // internally allocs inode 1
    kprintf("flat_init done\n");

    int root = flat_create(&flat_fs);
    kprintf("root = %d\n", root);
    if (root != ROOT_DIR)
        die("files_init: root dir must be 1");

    kprintf("root dir created\n");   
    for (int i = 0; embedded_files[i].name != 0; i++) {
        L1(L_NORM, L_ADD_FILE, i);
        const char *name = embedded_files[i].name;
        for (const char *p = name; *p != 0; p++)
            if (*p == '/') name = p + 1;
        struct dirent de;
        memset(&de, 0, sizeof(de));
        strncpy(de.name, name, NAME_LEN - 1);
        de.file = flat_create(&flat_fs);
        if (de.file != i + 2)
            die("files_init: unexpected file number");
        kprintf("adding file %d: %s\n", i, embedded_files[i].name);
        kprintf("  size=%d\n", embedded_files[i].size);
        flat_write(&flat_fs, de.file, 0, embedded_files[i].data,
                embedded_files[i].size);
        kprintf("  data written\n");
        flat_write(&flat_fs, ROOT_DIR, i * sizeof(de), &de, sizeof(de));
        kprintf("  dir entry written\n");
    }
    kprintf("all files added\n");
}