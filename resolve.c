#include "dogfs.h"

static char *
path_end(char *path)
{
    return strchrnul(path, '/');
}

static int
resolve_inode_s(statement_t *s, inode_t *inode)
{
    MYSQL_BIND row;
    bind_inode(&row, inode);
    if (statement_bind_result(s, &row))
        return -EINTERNAL;
    int r = statement_fetch(s);
    if (r == 1)
        return -ETRANSIENT;
    if (r == MYSQL_NO_DATA)
        return -ENOENT;

    return 0;
}

static inode_t
resolve_inode_lookup(connection_t *c, char *filename, size_t len, inode_t from)
{
    MYSQL_BIND bind[2];
    bind_string_len(bind + 0, filename, len);
    bind_inode(bind + 1, &from);
    inode_t inode;
    int r = run_with_statement(c,
                               "select inode from paths"
                               " where filename = ?"
                               "   and directory = ?", bind,
                               resolve_inode_s, &inode);
    if (r)
        return INVALID_INODE;
    icache_add(filename, len, from, inode);
    return inode;
}

static inode_t
resolve_inode_recurse(connection_t *c, char *path, inode_t from,
                      char **basename)
{
    if (*path == '\0')
        return from;

    if (*path == '/')
        return resolve_inode_recurse(c, path + 1, from, basename);

    char *end = path_end(path);

    if (basename && *end == '\0') {
        *basename = path;
        return from;
    }

    inode_t inode = icache_lookup(path, end - path, from);
    if (inode == INVALID_INODE)
        inode = resolve_inode_lookup(c, path, end - path, from);
    if (inode == INVALID_INODE)
        return INVALID_INODE;

    return resolve_inode_recurse(c, end, inode, basename);
}

inode_t
resolve_inode_from(connection_t *c, const char *path, inode_t from)
{
    return resolve_inode_recurse(c, (char *)path, from, NULL);
}

inode_t
resolve_inode(connection_t *c, const char *path)
{
    return resolve_inode_recurse(c, (char *)path, ROOT_INODE, NULL);
}

inode_t
resolve_dir_inode(connection_t *c, const char *path, char **basename)
{
    return resolve_inode_recurse(c, (char *)path, ROOT_INODE, basename);
}
