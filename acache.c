/* Attribute Cache
 *
 * Pretty much a simple write-through cache, except that we keep an
 * overlay of open, modified files separately.  Cache entries expire
 * after a short time to minimize disagreement between clients.
 */

#include "dogfs.h"

#define ACACHE_EXPIRE (5) // Seconds
#define ACACHE_ENTRIES (64)

static lru_t cache;

typedef struct acache_entry_t acache_entry_t;
struct acache_entry_t
{
    struct stat stat;           /* Must be first */
    struct timespec created;
};

static bool
inode_eq(void *x, void *y)
{
    inode_t *a = x;
    inode_t *b = y;
    return *a == *b;
}

static void
acache_entry_evict(void *k, void *v)
{
}

void
acache_remove(inode_t inode)
{
    lru_remove(&cache, &inode);
}

static void
acache_entry_init(acache_entry_t *e)
{
    wallclock(&e->created);
}

static bool
acache_live(acache_entry_t *e)
{
    struct timespec now;
    wallclock(&now);
    return now.tv_sec - e->created.tv_sec < ACACHE_EXPIRE;
}

static int
acache_get_impl(connection_t *c, inode_t inode, struct stat *stbuf)
{
    acache_entry_t entry;
    if (lru_lookup(&cache, &inode, (void*)&entry) && acache_live(&entry)) {
        memcpy(stbuf, &entry.stat, sizeof(*stbuf));
        return 0;
    }
    int r = inode_getattr(c, inode, stbuf);
    if (r == 0) {
        acache_entry_init(&entry);
        entry.stat = *stbuf;
        lru_add(&cache, &inode, &entry);
    }
    return r;
}

int
acache_get(connection_t *c, inode_t inode, struct stat *stbuf)
{
    int r = acache_get_impl(c, inode, stbuf);
    if (r == 0)
        file_getattr(inode, stbuf);
    return r;
}

static void
acache_dump_stats(void)
{
    log("Attribute cache entries:");
    lru_foreach(e, &cache) {
        inode_t *i = lru_entry_key(&cache, e);
        acache_entry_t *a = lru_entry_value(&cache, e);
        log("  %llu: mtime %ld, size %ld, hits %ld",
            *i, a->stat.st_mtime, a->stat.st_size, e->hits);
    }
    lru_dump_stats(&cache);
}
SET_ENTRY(debug_hooks, acache_dump_stats);

static void
acache_init(void)
{
    lru_init(&cache, ACACHE_ENTRIES, sizeof(inode_t), inode_eq,
             sizeof(struct acache_entry_t), NULL, acache_entry_evict);
}
SET_ENTRY(init_hooks, acache_init);
