#include "dogfs.h"

static int
inode_unlink_count(statement_t *s, unsigned long *count)
{
    MYSQL_BIND row;
    bind_ulong(&row, count);
    if (statement_bind_result(s, &row))
        return -EINTERNAL;
    int r = statement_fetch(s);
    if (r == 1)
        return -ETRANSIENT;
    if (r == MYSQL_NO_DATA)
        return -EINTERNAL;
    return 0;
}

int
inode_unlink_trx(connection_t *c, inode_t dir, inode_t i, char *basename)
{
    cache_remove(i);

    MYSQL_BIND params1[2];
    bind_inode(params1 + 0, &dir);
    bind_string(params1 + 1, basename);
    int r = run_statement(c,
                          "delete from paths where directory = ?"
                          "   and filename = ?", params1);
    if (r) return r;

    MYSQL_BIND params2;
    bind_inode(&params2, &i);
    unsigned long count;
    r = run_with_statement(c, "select count(*) from paths where inode = ?",
                           &params2, inode_unlink_count, &count);
    if (r) return r;

    if (count == 0)
        r = run_statement(c, "insert into deleted_files values (?)", &params2);
    return r;
}

static int
inode_unlink(connection_t *c, inode_t dir, inode_t i, char *basename)
{
    return run_with_trx(inode_unlink_trx, c, dir, i, basename);
}

static int
dogfs_unlink_c(connection_t *c, const char *path)
{
    char *basename;
    inode_t dir = resolve_dir_inode(c, path, &basename);
    if (dir == INVALID_INODE)
        return -ENOENT;
    inode_t i = resolve_inode_from(c, basename, dir);
    if (i == INVALID_INODE)
        return -ENOENT;
    return inode_unlink(c, dir, i, basename);
}

int
dogfs_unlink(const char *path)
{
    return run_with_connection(dogfs_unlink_c, path);
}
