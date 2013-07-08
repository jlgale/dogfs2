#include "dogfs.h"

static histogram_t reads;

int
dogfs_read(const char *path, char *buf, size_t len, off_t off,
           struct fuse_file_info *fi)
{
#ifdef SIZE_STATS
    histogram_add(&reads, len);
#endif
    file_t *f = get_file(fi);
    int r = file_flush(f);
    if (r < 0)
        return r;
    return run_with_connection(inode_read, f->i, buf, len, off, true);
}

static void
read_dump_stats(void)
{
    log("Read sizes:");
    histogram_dump(&reads);
}
SET_ENTRY(debug_hooks, read_dump_stats);

static void
read_init(void)
{
    histogram_init(&reads);
}
SET_ENTRY(init_hooks, read_init);
