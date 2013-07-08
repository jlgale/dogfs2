typedef struct file_t file_t;

struct file_t
{
    inode_t i;
    int refcount;      /* Multiple open() calls share this context. */
    connection_t *c;
    pthread_mutex_t lock; /* Protects fields below, held during fsync. */
    ring_t open;          /* Ring of all open files. */
    ring_t modified;            /* Ring of all modified files. */
    bool truncated;
    off_t size;
    struct timespec mtime;
    struct timespec ctime;

    off_t write_off;
    size_t write_len;
    char *write_buf;
};

static inline file_t *
get_file(struct fuse_file_info *fi)
{
    return (file_t *)fi->fh;
}

void file_opened(struct fuse_file_info *fi, connection_t *c, inode_t inode);
void file_released(file_t *f);
void file_getattr(inode_t inode, struct stat *stbuf);
void file_truncated(file_t *f, off_t newsize);
void file_synced(file_t *f);
int file_flush(file_t *f);
int file_flush_locked(file_t *f);
int file_write(file_t *f, const char *buf, size_t len, off_t off);
