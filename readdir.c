#include "dogfs.h"

#define MAXFILENAME 1024 /* XXX */

static int
inode_readdir_s(statement_t *s, void *buf, fuse_fill_dir_t filler)
{
    char file[MAXFILENAME];
    unsigned long filelen;
    MYSQL_BIND row;
    memset(&row, 0, sizeof(row));
    row.buffer_type = MYSQL_TYPE_STRING; // data
    row.length = &filelen;
    row.buffer = file;
    row.buffer_length = sizeof(file);
    while (1) {
        /* XXX - do we hafta call every time? */
        if (statement_bind_result(s, &row))
            return -EINTERNAL;
        int r = statement_fetch(s);
        if (r == 1)
            return -ETRANSIENT;
        if (r == MYSQL_NO_DATA)
            return 0;
        /* XXX - get stats and offset too */
        filler(buf, file, NULL, 0);
    }
}

static int
inode_readdir(connection_t *c, inode_t inode, void *buf,
              fuse_fill_dir_t filler)
{
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    MYSQL_BIND param;
    bind_inode(&param, &inode);
    return run_with_statement(c,
                              "select filename from paths"
                              " where directory = ?", &param,
                              inode_readdir_s, buf, filler);

}

int
dogfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
              off_t offset, struct fuse_file_info *fi)
{
    /* XXX - use offset */
    (void) offset;
    (void) fi;

    return run_with_c_inode(path, inode_readdir, buf, filler);
}
