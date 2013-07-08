#include "dogfs.h"

int
dogfs_release(const char *path, struct fuse_file_info *fi)
{
    file_t *f = get_file(fi);
    /* XXX - locking */
    file_released(f);
    return 0;
}
