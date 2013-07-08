#include "dogfs.h"

#define CLEANUP_SECONDS 100

static int
run_sql(connection_t *c, char *sql)
{
    int r = mysql_query(c->c, sql);
    if (r) {
        vlog("query error `%s`: %s", sql, mysql_error(c->c));
        return -1;
    }
    return 0;
}

static int
cleanup_c(connection_t *c)
{
    while (1) {
        int r = run_sql(c,
                        "delete from files"
                        " where inode in (select inode"
                        "                   from deleted_files limit 100)");
        if (r)
            return r;
        if (mysql_affected_rows(c->c) == 0)
            return 0;
    }
}

static void *
cleanup_loop(void *x)
{
    while (1) {
        sleep(CLEANUP_SECONDS);
        connection_t *c = connection_open_raw();
        int r = cleanup_c(c);
        connection_close(c);
        if (r)
            vlog("cleanup error: %s", strerror(-r));
    }
    return NULL;
}

static void
cleanup_init(void)
{
    pthread_t t;
    thread_create(&t, cleanup_loop, NULL);
}
SET_ENTRY(init_hooks, cleanup_init);
