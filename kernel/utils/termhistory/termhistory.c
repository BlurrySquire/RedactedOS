#include "files/system_module.h"
#include "files/stack_fs.h"

system_module termhistory_mod = {
    .name = "terminal history",
    .mount = "termhistory",
    .version = VERSION_NUM(0, 1, 0, 0),
    .init = stackfs_init,
    .open = stackfs_open,
    .read = stackfs_read,
    .write = stackfs_write,
    .readdir = stackfs_readdir,
    .getstat = stackfs_stat
};