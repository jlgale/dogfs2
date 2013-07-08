#include "dogfs.h"

static int
dogfs_mkdir_c(connection_t *c, const char *path, mode_t mode)
{
    char *basename;
    inode_t dir = resolve_dir_inode(c, path, &basename);
    if (dir == INVALID_INODE)
        return -ENOENT;
    inode_t inode;
    return inode_create(c, dir, basename, mode|S_IFDIR, &inode);
}

int
dogfs_mkdir(const char *path, mode_t mode)
{
    return run_with_connection(dogfs_mkdir_c, path, mode);
}
