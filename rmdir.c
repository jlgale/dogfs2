#include "dogfs.h"

static int
inode_rmdir_check(statement_t *s)
{
    long dummy;
    MYSQL_BIND row;
    bind_long(&row, &dummy);
    if (statement_bind_result(s, &row))
        return -EINTERNAL;
    int r = statement_fetch(s);
    if (r == 1)
        return -ETRANSIENT;
    if (r == MYSQL_NO_DATA)
        return 0;
    return -ENOTEMPTY;
}

/* Unlink a directory (or regular file) */
int
inode_rmdir_trx(connection_t *c, inode_t dir, inode_t i, char *basename)
{
    MYSQL_BIND param;
    bind_inode(&param, &i);
    int r = run_with_statement(c,
                               "select 1 from paths"
                               " where directory = ? limit 1",
                               &param, inode_rmdir_check);
    if (r) return r;
    return inode_unlink_trx(c, dir, i, basename);
}

static int
inode_rmdir(connection_t *c, inode_t dir, inode_t i, char *basename)
{
    return run_with_trx(inode_rmdir_trx, c, dir, i, basename);
}

static int
dogfs_rmdir_c(connection_t *c, const char *path)
{
    char *basename;
    inode_t dir = resolve_dir_inode(c, path, &basename);
    if (dir == INVALID_INODE)
        return -ENOENT;
    inode_t i = resolve_inode_from(c, basename, dir);
    if (i == INVALID_INODE)
        return -ENOENT;
    return inode_rmdir(c, dir, i, basename);
}

int
dogfs_rmdir(const char *path)
{
    return run_with_connection(dogfs_rmdir_c, path);
}
