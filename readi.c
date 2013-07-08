#include "dogfs.h"

static long
fill_zeros(char *buf, long from, long to)
{
    size_t len = to - from;
    memset(buf, 0, len);
    return len;
}

static int
inode_read_fill(statement_t *s, char *buf, size_t len, off_t off, bool pad)
{
    MYSQL_BIND row[2];
    memset(row, 0, sizeof(row));
    long block_offset;
    bind_long(row, &block_offset);
    row[1].buffer_type = MYSQL_TYPE_BLOB; // data
    size_t blocklen;
    long read = 0;
    while (1) {
        row[1].length = &blocklen;
        row[1].buffer = 0;
        row[1].buffer_length = 0; /* Delay read */
        /* XXX - do we hafta call every time? */
        if (statement_bind_result(s, row))
            return -EINTERNAL;
        int r = statement_fetch(s);
        if (r == 1)
            return -ETRANSIENT;
        if (r == MYSQL_NO_DATA) {
            if (pad)
                read += fill_zeros(buf + read, off + read, off + len);
            return read;
        }
        read += fill_zeros(buf + read, off + read, block_offset);
        row[1].buffer = buf + read;
        row[1].buffer_length = len - read;
        if (mysql_stmt_fetch_column(s->s, row + 1, 1, 0))
            return -ETRANSIENT;
        read += (blocklen > len - read) ? len - read : blocklen;
    }
}

int
inode_read(connection_t *c, inode_t inode, char *buf, size_t len, off_t off,
           bool pad)
{
    off_t end = off + len;
    MYSQL_BIND params[3];
    bind_inode(params + 0, &inode);
    bind_long(params + 1, &off);
    bind_long(params + 2, &end);
    return run_with_statement(c,
                              "select `offset`, data from blocks"
                              " where inode = ?"
                              "   and `offset` >= ?"
                              "   and `offset` < ?"
                              " order by `offset`", params,
                              inode_read_fill, buf, len, off, pad);
}

