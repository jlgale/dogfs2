#include "dogfs.h"

static int
inode_getattr_s(statement_t *s, inode_t inode, struct stat *stbuf)
{
    unsigned int uid, gid, mode, flags;
    long atime, mtime, ctime, size;
    my_bool size_null;
    MYSQL_BIND row[8];
    bind_uint(row + 0, &uid);
    bind_uint(row + 1, &gid);
    bind_uint(row + 2, &mode);
    bind_long(row + 3, &atime);
    bind_long(row + 4, &mtime);
    bind_long(row + 5, &ctime);
    bind_uint(row + 6, &flags);
    bind_long(row + 7, &size);
    row[7].is_null = &size_null;
    if (statement_bind_result(s, row))
        return -EINTERNAL;
    int r = statement_fetch(s);
    if (r == 1)
        return -ETRANSIENT;
    if (r == MYSQL_NO_DATA)
        return -ENOENT;

    if (size_null) /* null means empty */
        size = 0;

    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_ino = inode;     /* inode number */
    stbuf->st_mode = mode;     /* protection */
    stbuf->st_nlink = 1;       /* number of hard links, XXX */
    stbuf->st_uid = uid;       /* user ID of owner */
    stbuf->st_gid = gid;       /* group ID of owner */
    stbuf->st_size = size;     /* total size, in bytes */
    stbuf->st_atime = atime;   /* time of last access */
    stbuf->st_mtime = mtime;   /* time of last modification */
    stbuf->st_ctime = ctime;   /* time of last status change */
    stbuf->st_blksize = BLOCKSIZE; /* preferred io blocksize */

    return 0;
}

int
inode_getattr(connection_t *c, inode_t inode, struct stat *stbuf)
{
    MYSQL_BIND param;
    bind_inode(&param, &inode);
    return run_with_statement(c,
                              "select uid, gid, mode,"
                              "       unix_timestamp(atime),"
                              "       unix_timestamp(mtime),"
                              "       unix_timestamp(ctime),"
                              "       flags,"
                              "       (select max(offset) + length(data)"
                              "          from blocks"
                              "         where inode = files.inode) size"
                              "  from files"
                              " where inode = ?", &param,
                              inode_getattr_s, inode, stbuf);
}

int
dogfs_getattr(const char *path, struct stat *stbuf)
{
    return run_with_c_inode(path, acache_get, stbuf);
}

int
dogfs_fgetattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    file_t *f = get_file(fi);
    return run_with_connection(acache_get, f->i, stbuf);
}
