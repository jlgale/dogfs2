#include "dogfs.h"

static int
dogfs_chmod_c(connection_t *c, inode_t inode, mode_t mode)
{
    MYSQL_BIND params[2];
    bind_uint(params + 0, &mode);
    bind_inode(params + 1, &inode);
    return run_with_statement(c, "update files set mode = ? where inode = ?",
                              params, check_update);
}

int
dogfs_chmod(const char *path, mode_t mode)
{
    return run_with_c_inode_updating(path, dogfs_chmod_c, mode);
}
