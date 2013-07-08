#include "dogfs.h"

static histogram_t writes;

static void
bind_write_data(MYSQL_BIND *bind, const char *buf, size_t len)
{
    memset(bind, 0, sizeof(*bind));
    bind->buffer_type = MYSQL_TYPE_BLOB; // data
    bind->buffer = (char *)buf;
    bind->buffer_length = len;
}

static int
inode_write_merge(connection_t *c, inode_t inode, const char *buf,
                  off_t len, off_t off);

static void
bind_row(MYSQL_BIND *row, inode_t *inode, const char *buf,
         size_t len, size_t *off)
{
   bind_inode(row + 0, inode);
   bind_ulong(row + 1, off);
   bind_write_data(row + 2, buf, len);
}

/* Write the given block to a file.  Assumes off is BLOCKSIZE aligned.
 * If we're writing a full block, we simply replace the existing
 * block, if any.  If we're writing a short block, we optimisticly
 * attempt to insert, but if we encounter an existing block we have to
 * merge it with the exising.
 */
static int
inode_write_each(connection_t *c, inode_t inode, const char *buf, size_t len,
                 size_t off, bool overwrite)
{
    assert(off % BLOCKSIZE == 0);
    assert(len > 0);
    if (len > BLOCKSIZE)
        len = BLOCKSIZE;
    MYSQL_BIND params[3];
    bind_row(params, &inode, buf, len, &off);
    int r;
    if (len == BLOCKSIZE || overwrite) {
        r = run_statement(c, "replace into blocks values (?, ?, binary ?)",
                          params);
    } else {
        r = run_statement(c, "insert into blocks values (?, ?, binary ?)",
                          params);
        if (r == -EEXIST)
            return inode_write_merge(c, inode, buf, len, off);
    }
    return r < 0 ? r : (int)len;
}

/* Since we statically declare prepared statements, use a macro to
 * stamp out multi-row writes.
 */
#define INODE_WRITE(n)                          \
    static int                                                          \
    inode_write_ ## n(connection_t *c, inode_t inode, const char *buf,  \
                      size_t len, size_t off)                           \
    {                                                                   \
        assert(len >= BLOCKSIZE * n);                                   \
        size_t offsets[n];                                              \
        MYSQL_BIND params[3 * n];                                       \
        for (int i = 0; i < n; ++i) {                                   \
            offsets[i] = off + BLOCKSIZE * i;                           \
            bind_row(params + 3 * i, &inode,                            \
                     buf + BLOCKSIZE * i, BLOCKSIZE, offsets + i);      \
        }                                                               \
        int r = run_statement(c, "replace into blocks values"           \
                              PREPARED_ROW_ ## n, params);              \
        return r < 0 ? r : BLOCKSIZE * n;                               \
    }

#define PREPARED_ROW_1 " (?, ?, binary ?) "
#define PREPARED_ROW_2 PREPARED_ROW_1 "," PREPARED_ROW_1
INODE_WRITE(2);
#define PREPARED_ROW_4 PREPARED_ROW_2 "," PREPARED_ROW_2
INODE_WRITE(4);
#define PREPARED_ROW_8 PREPARED_ROW_4 "," PREPARED_ROW_4
INODE_WRITE(8);
#define PREPARED_ROW_16 PREPARED_ROW_8 "," PREPARED_ROW_8
INODE_WRITE(16);
#define PREPARED_ROW_32 PREPARED_ROW_16 "," PREPARED_ROW_16
INODE_WRITE(32);

static int
inode_write_1(connection_t *c, inode_t inode, const char *buf, size_t len,
              size_t off)
{
    return inode_write_each(c, inode, buf, len, off, false);
}

static int
inode_write_rows(connection_t *c, inode_t inode, const char *buf, size_t len,
                 size_t off)
{
    assert(off % BLOCKSIZE == 0);
    if (len >= BLOCKSIZE * 32)
        return inode_write_32(c, inode, buf, len, off);
    else if (len >= BLOCKSIZE * 16)
        return inode_write_16(c, inode, buf, len, off);
    else if (len >= BLOCKSIZE * 8)
        return inode_write_8(c, inode, buf, len, off);
    else if (len >= BLOCKSIZE * 4)
        return inode_write_4(c, inode, buf, len, off);
    else if (len >= BLOCKSIZE * 2)
        return inode_write_2(c, inode, buf, len, off);
    else
        return inode_write_1(c, inode, buf, len, off);
}

static int
inode_write_merge(connection_t *c, inode_t inode, const char *buf,
                  off_t len, off_t off)
{
    off_t block_off = off % BLOCKSIZE;
    if (block_off || len < BLOCKSIZE) {
        off_t firstoff = off - block_off;
        char first[BLOCKSIZE];
        off_t firstlen = MIN(BLOCKSIZE, len + block_off);
        off_t firstuse = firstlen - block_off;
        int r = inode_read(c, inode, first, BLOCKSIZE, firstoff, false);
        if (r < 0)
            return r;
        if (block_off > r)
            memset(first + r, 0, block_off - r);
        memcpy(first + block_off, buf, firstuse);
        r = inode_write_each(c, inode, first, MAX(r, firstlen),
                             firstoff, true);
        if (r < 0)
            return r;
        return firstuse;
    } else {
        return 0;
    }
}

int
inode_write(connection_t *c, inode_t inode, const char *buf,
            size_t len, off_t off)
{
#ifdef SIZE_STATS
    histogram_add(&writes, len);
#endif
    int r = inode_write_merge(c, inode, buf, len, off);
    if (r < 0)
        return r;
    size_t written = r;
    while (written < len) {
        size_t writelen = len - written;
        r = inode_write_rows(c, inode, buf + written, writelen, off + written);
        if (r < 0)
            return r;
        written += r;
    }
    return written;
}

static void
write_dump_stats(void)
{
    log("Actual write sizes:");
    histogram_dump(&writes);
}
SET_ENTRY(debug_hooks, write_dump_stats);

static void
write_init(void)
{
    histogram_init(&writes);
}
SET_ENTRY(init_hooks, write_init);
