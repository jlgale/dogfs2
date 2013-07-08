#include "dogfs.h"

static int
inode_open(connection_t *c, inode_t inode, struct fuse_file_info *fi)
{
    /* XXX - locking */
    file_opened(fi, c, inode);
    return 0;
}

int
dogfs_open(const char *path, struct fuse_file_info *fi)
{
    connection_t *c = connection_acquire();
    if (!c)
        return -ETRANSIENT;
    inode_t inode = resolve_inode(c, path);
    if (inode == INVALID_INODE) {
        connection_release(c);
        return -EEXIST;
    }
    return inode_open(c, inode, fi);
}
