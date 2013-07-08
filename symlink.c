#include "dogfs.h"

static int
dogfs_symlink_trx(connection_t *c, const char *target, const char *source)
{
    char *basename;
    inode_t dir = resolve_dir_inode(c, source, &basename);
    if (dir == INVALID_INODE)
        return -ENOENT;
    inode_t inode;
    int r = inode_create(c, dir, basename, S_IFLNK | 0777, &inode);
    if (r)
        return r;
    MYSQL_BIND params[2];
    bind_inode(params + 0, &inode);
    bind_string(params + 1, target);
    return run_statement(c, "insert into symlinks (inode, target)"
                         " values (?, ?)", params);
}

static int
dogfs_symlink_c(connection_t *c, const char *target, const char *source)
{
    return run_with_trx(dogfs_symlink_trx, c, target, source);
}

int
dogfs_symlink(const char *target, const char *source)
{
    return run_with_connection(dogfs_symlink_c, target, source);
}
