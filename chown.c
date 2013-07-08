#include "dogfs.h"

static int
dogfs_chown_c(connection_t *c, inode_t inode, uid_t uid, gid_t gid)
{
    MYSQL_BIND params[3];
    bind_uint(params + 0, &uid);
    bind_uint(params + 1, &gid);
    bind_inode(params + 2, &inode);
    return run_with_statement(c, "update files set uid = ?, gid = ?"
                               " where inode = ?", params, check_update);
}

int
dogfs_chown(const char *path, uid_t uid, gid_t gid)
{
    return run_with_c_inode_updating(path, dogfs_chown_c, uid, gid);
}
