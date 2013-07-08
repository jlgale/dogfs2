#include "dogfs.h"

static int
inode_utimens(connection_t *c, inode_t inode,
              const struct timespec *atime,
              const struct timespec *mtime)
{
    MYSQL_BIND params[3];
    bind_long(params + 0, (void *)&atime->tv_sec);
    bind_long(params + 1, (void *)&mtime->tv_sec);
    bind_inode(params + 2, &inode);
    return run_with_statement(c,
                              "update files set atime = from_unixtime(?),"
                              "                 mtime = from_unixtime(?)"
                              " where inode = ?", params, check_update);
}

int
dogfs_utimens(const char *path, const struct timespec tv[2])
{
    return run_with_c_inode_updating(path, inode_utimens, tv + 0, tv + 1);
}
