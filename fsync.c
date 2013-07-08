#include "dogfs.h"

/* XXX - does fuse handle locking around fsync/flush?  We assume not. */
static int
dogfs_fsync_c(connection_t *c, file_t *f, int datasync)
{
    MUTEX_LOCK_FOR_SCOPE(f->lock);

    int r = file_flush_locked(f);
    if (r < 0 || datasync)
        return r;

    MYSQL_BIND params[2];
    bind_long(params + 0, &f->mtime.tv_sec);
    bind_inode(params + 1, &f->i);
    r = run_statement(c,
                      "update files"
                      "   set mtime = from_unixtime(?)"
                      " where inode = ?", params);
    if (r == 0) {
        file_synced(f);
        acache_remove(f->i);
    }
    return r;
}

int
dogfs_fsync(const char *path, int datasync, struct fuse_file_info *fi)
{
    file_t *f = get_file(fi);
    if (ring_is_empty(&f->modified))
        return 0;
    return run_with_connection(dogfs_fsync_c, f, datasync);
}

/* XXX - not sure about the distinction between fsync and flush */
int
dogfs_flush(const char *path, struct fuse_file_info *fi)
{
    return dogfs_fsync(path, 0, fi);
}
