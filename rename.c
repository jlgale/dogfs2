#include "dogfs.h"

static int
dogfs_rename_trx(connection_t *c, const char *from, const char *to)
{
    char *from_file, *to_file;
    inode_t from_dir = resolve_dir_inode(c, from, &from_file);
    if (from_file == INVALID_INODE)
        return -ENOENT;
    inode_t to_dir = resolve_dir_inode(c, to, &to_file);
    if (to_file == INVALID_INODE)
        return -ENOENT;
    inode_t existing = resolve_inode_from(c, to_file, to_dir);
    if (existing != INVALID_INODE) {
        int r = inode_rmdir_trx(c, to_dir, existing, to_file);
        if (r) return r;
    }

    inode_t inode = resolve_inode_from(c, from_file, from_dir);
    if (inode == INVALID_INODE)
        return -ENOENT;
    cache_remove(inode);

    MYSQL_BIND params[4];
    bind_inode(params + 0, &to_dir);
    bind_string(params + 1, to_file);
    bind_inode(params + 2, &from_dir);
    bind_string(params + 3, from_file);
    return run_with_statement(c,
                              "update paths set directory=?, filename=?"
                              "  where directory=? and filename=?", params,
                              check_update);
}

static int
dogfs_rename_c(connection_t *c, const char *from, const char *to)
{
    return run_with_trx(dogfs_rename_trx, c, from, to);
}

int
dogfs_rename(const char *from, const char *to)
{
    return run_with_connection(dogfs_rename_c, from, to);
}
