#include "dogfs.h"

static int
inode_readlink_s(statement_t *s, char *buf, size_t len)
{
    size_t tgtlen;
    MYSQL_BIND tgt;
    bind_string_len(&tgt, buf, len - 1 /* reserve null byte */);
    tgt.length = &tgtlen;
    if (statement_bind_result(s, &tgt))
        return -EINTERNAL;
    int r = statement_fetch(s);
    if (r == 1)
        return -ETRANSIENT;
    if (r == MYSQL_NO_DATA)
        return -ENOENT;
    buf[tgtlen] = '\0'; /* set null byte */
    return 0;
}

static int
inode_readlink(connection_t *c, inode_t inode, char *buf, size_t len)
{
    MYSQL_BIND param;
    bind_inode(&param, &inode);
    return run_with_statement(c, "select target from symlinks"
                              " where inode = ?", &param,
                              inode_readlink_s, buf, len);
}

int
dogfs_readlink(const char *path, char *buf, size_t len)
{
    return run_with_c_inode(path, inode_readlink, buf, len);
}
