#include "dogfs.h"

static int
dogfs_truncate_blocks(connection_t *c, inode_t inode, off_t newsize)
{
    if (newsize != 0) /* XXX - NYI */
        return -EINVAL;
    MYSQL_BIND param;
    bind_inode(&param, &inode);
    return run_statement(c, "delete from blocks where inode = ?", &param);
}

static int
dogfs_truncate_trx(connection_t *c, inode_t inode, off_t newsize)
{
    MYSQL_BIND params[1];
    bind_inode(params + 0, &inode);
    int r = run_statement(c,
                          "update files set ctime = now()"
                          " where inode = ?", params);
    if (r)
        return r;
    return dogfs_truncate_blocks(c, inode, newsize);
}

static int
inode_truncate(connection_t *c, inode_t inode, off_t newsize)
{
    return run_with_trx(dogfs_truncate_trx, c, inode, newsize);
}

int
dogfs_truncate(const char *path, off_t newsize)
{
    return run_with_c_inode_updating(path, inode_truncate, newsize);
}

static int
dogfs_ftruncate_c(connection_t *c, file_t *f, off_t newsize)
{
    int r = dogfs_truncate_blocks(c, f->i, newsize);
    if (r == 0)
        file_truncated(f, newsize);
    return r;
}

int
dogfs_ftruncate(const char *path, off_t newsize, struct fuse_file_info *fi)
{
    file_t *f = get_file(fi);
    file_flush(f);
    return run_with_connection(dogfs_ftruncate_c, f, newsize);
}
