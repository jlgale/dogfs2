#include "dogfs.h"

static int
inode_create_file(statement_t *s, inode_t *inode)
{
    *inode = mysql_stmt_insert_id(s->s);
    return 0;
}

int
inode_create(connection_t *c, inode_t dir, char *basename, mode_t mode,
             inode_t *inode)
{
    struct fuse_context *fc = fuse_get_context();
    MYSQL_BIND params[3];
    bind_uint(params + 0, &fc->uid);
    bind_uint(params + 1, &fc->gid);
    bind_uint(params + 2, &mode);
    int r = run_with_statement(c,
                               "insert into files (uid, gid, mode)"
                               " values (?, ?, ?)", params,
                               inode_create_file, inode);
    if (r)
        return r;
    bind_inode(params + 0, &dir);
    bind_string(params + 1, basename);
    r = run_statement(c,
                      "insert into paths (directory, filename, inode)"
                      " select ?, ?, (@dogfs_open := last_insert_id())",
                      params);
    return r;
}

int
dogfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    connection_t *c = connection_acquire();
    if (!c)
        return -ETRANSIENT;
    char *basename;
    inode_t dir = resolve_dir_inode(c, path, &basename);
    if (dir == INVALID_INODE) {
        connection_release(c);
        return -EEXIST;
    }
    inode_t inode;
    int r = inode_create(c, dir, basename, mode, &inode);
    if (r) {
        connection_error(c);
        connection_release(c);
        return r;
    }
    file_opened(fi, c, inode);
    return 0;
}
