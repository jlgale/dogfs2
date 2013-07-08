#include "dogfs.h"

static histogram_t writes;

int
dogfs_write(const char *path, const char *buf, size_t len, off_t off,
            struct fuse_file_info *fi)
{
#ifdef SIZE_STATS
    histogram_add(&writes, len);
#endif
    file_t *f = get_file(fi);
    return file_write(f, buf, len, off);
}

static void
write_dump_stats(void)
{
    log("Request write sizes:");
    histogram_dump(&writes);
}
SET_ENTRY(debug_hooks, write_dump_stats);

static void
write_init(void)
{
    histogram_init(&writes);
}
SET_ENTRY(init_hooks, write_init);
