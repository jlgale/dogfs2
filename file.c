#include "dogfs.h"

#define WRITE_BUFFER_SIZE (BLOCKSIZE * 32)

static pthread_spinlock_t lock;
static ring_t opened;
static ring_t modified;

static void
file_free(file_t *f)
{
    pthread_mutex_destroy(&f->lock);
    assert(ring_is_empty(&f->modified));
    connection_release(f->c);
    free(f);
}

static file_t *
file_create(connection_t *c, inode_t inode)
{
    file_t *f = malloc(sizeof(*f));
    f->c = c;
    f->i = inode;
    f->truncated = false;
    f->write_off = 0;
    f->write_len = 0;
    f->write_buf = NULL;
    f->refcount = 1;
    f->size = 0;
    ring_init(&f->modified);
    mutex_init(&f->lock);
    return f;
}

static bool
opened_file_deref(file_t *f)
{
    SPIN_LOCK_FOR_SCOPE(lock);
    if (--f->refcount == 0) {
        ring_delete(open, f);
        return true;
    }
    return false;
}

static file_t *
opened_file_find_or_insert(file_t *f)
{
    SPIN_LOCK_FOR_SCOPE(lock);
    ring_foreach_mutable_decl(&opened, open, file_t, e) {
        if (e->i == f->i) {
            e->refcount++;
            return e;
        }
    }
    ring_append(&opened, open, f);
    return NULL;
}

/* XXX - connection is vestigial; someday we'll use it for locking the
 * file. */
void
file_opened(struct fuse_file_info *fi, connection_t *c, inode_t inode)
{
    file_t *f = file_create(c, inode);
    file_t *e = opened_file_find_or_insert(f);
    if (e) {
        file_free(f);
        f = e;
    }
    fi->fh = (uint64_t)f;
}

void
file_released(file_t *f)
{
    if (opened_file_deref(f))
        file_free(f);
}

static void
file_touch_locked(file_t *f)
{
    if (ring_is_empty(&f->modified)) {
        SPIN_LOCK_FOR_SCOPE(lock);
        ring_append(&modified, modified, f);
    }
    wallclock(&f->mtime);
    f->ctime = f->mtime;
}

void
file_truncated(file_t *f, off_t newsize)
{
    MUTEX_LOCK_FOR_SCOPE(f->lock);
    file_touch_locked(f);
    f->truncated = true;
    f->size = newsize;
}

static bool
file_write_compatible(file_t *f, off_t off)
{
    return !f->write_buf || f->write_off + (off_t)f->write_len == off;
}

static size_t
file_write_available(file_t *f)
{
    return WRITE_BUFFER_SIZE - f->write_len;
}

static int
file_flush_c(connection_t *c, file_t *f)
{
    size_t offset = f->write_off % BLOCKSIZE;
    if (offset) {
        int r = inode_read(c, f->i, f->write_buf, offset,
                           f->write_off - offset, true);
        if (r < 0)
            return r;
    }
    assert(f->write_len <= WRITE_BUFFER_SIZE);
    return inode_write(c, f->i, f->write_buf, f->write_len,
                       f->write_off - offset);
}

int
file_flush_locked(file_t *f)
{
    if (!f->write_buf)
        return 0;
    int r = run_with_connection(file_flush_c, f);
    if (r < 0)
        return r;
    SPIN_LOCK_FOR_SCOPE(lock); /* XXX - around write_buf*/
    free(f->write_buf);
    f->write_buf = NULL;
    return 0;
}

int
file_flush(file_t *f)
{
    if (!f->write_buf)
        return 0;
    MUTEX_LOCK_FOR_SCOPE(f->lock);
    return file_flush_locked(f);
}

static int
file_write_impl(file_t *f, const char *buf, size_t len, off_t off)
{
    if (!file_write_compatible(f, off)) {
        int r = file_flush_locked(f);
        if (r < 0)
            return r;
    }
    int written = 0;
    if (f->write_buf) {
        written = MIN(len, file_write_available(f));
        memcpy(f->write_buf + f->write_len, buf, written);
        f->write_len += written;
        buf += written;
        len -= written;
        off += written;
        if (f->write_len == WRITE_BUFFER_SIZE) {
            int r = file_flush_locked(f);
            if (r < 0)
                return r;
        }
        if (len == 0)
            return written;
    }
    int boffset = off % BLOCKSIZE;
    if (true || boffset + len >= WRITE_BUFFER_SIZE) {
        int r = run_with_connection(inode_write, f->i, buf, len, off);
        return r < 0 ? r : written + r;
    }
    /* XXX - locking around write_buf? */
    f->write_buf = malloc(WRITE_BUFFER_SIZE);
    f->write_off = off;
    f->write_len = boffset + len;
    memcpy(f->write_buf + boffset, buf, len);
    return written + len;
}

int
file_write(file_t *f, const char *buf, size_t len, off_t off)
{
    MUTEX_LOCK_FOR_SCOPE(f->lock);
    int r = file_write_impl(f, buf, len, off);
    if (r > 0) {
        file_touch_locked(f);
        off_t end = off + len;
        if (end > f->size)
            f->size = end;
    }
    return r;
}

static void
file_getattr_impl(file_t *f, struct stat *stbuf)
{
    /* XXX - grabbing f->lock is lock inversion if lock is held */
    stbuf->st_mtim = f->mtime;
    stbuf->st_ctim = f->ctime;
    if (f->truncated || f->size > stbuf->st_size)
        stbuf->st_size = f->size;
}

void
file_getattr(inode_t inode, struct stat *stbuf)
{
    SPIN_LOCK_FOR_SCOPE(lock);
    ring_foreach_decl(&modified, modified, file_t, f) {
        if (f->i == inode)
            file_getattr_impl(f, stbuf);
    }
}

/* Assumes f->lock is held. */
void
file_synced(file_t *f)
{
    SPIN_LOCK_FOR_SCOPE(lock);
    ring_delete(modified, f);
    ring_init(&f->modified);
    f->truncated = false;
}

static void
file_init(void)
{
    pthread_spin_init(&lock, 0);
    ring_init(&modified);
    ring_init(&opened);
}
SET_ENTRY(init_hooks, file_init);

static void
file_dump_stats(void)
{
    log("Modified files:");
    SPIN_LOCK_FOR_SCOPE(lock);
    ring_foreach_decl(&modified, modified, file_t, f) {
        log("  %llu", f->i);
    }
}
SET_ENTRY(debug_hooks, file_dump_stats);
