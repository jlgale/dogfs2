/* A simple inode cache. */
#include "dogfs.h"

typedef struct ikey_t ikey_t;
struct ikey_t
{
    inode_t dir;
    size_t file_len;
    char *file;
};

#define ICACHE_ENTRIES (64)

static lru_t cache;

inode_t
icache_lookup(char *file, size_t len, inode_t from)
{
    inode_t found = INVALID_INODE;
    ikey_t key = { .dir = from, .file_len = len, .file = file };
    lru_lookup(&cache, &key, (void *)&found);
    return found;
}

void
icache_add(char *file, size_t len, inode_t from, inode_t inode)
{
    ikey_t key = { .dir = from,
                   .file_len = len,
                   .file = strndup(file, len) };
    lru_add(&cache, &key, &inode);
}

void
icache_remove(inode_t inode)
{
    lru_remove_value(&cache, &inode);
}

static void
icache_dump_stats(void)
{
    log("Inode cache entries:");
    lru_foreach(e, &cache) {
        ikey_t *k = lru_entry_key(&cache, e);
        inode_t *i = lru_entry_value(&cache, e);
        log("  %llu, %s -> %llu (hits %ld)",
            k->dir, k->file, *i, e->hits);
    }
    lru_dump_stats(&cache);
}
SET_ENTRY(debug_hooks, icache_dump_stats);

static bool
ikey_eq(void *x, void *y)
{
    ikey_t *a = x;
    ikey_t *b = y;
    return a->dir == b->dir &&
        a->file_len == b->file_len &&
        strncmp(a->file, b->file, a->file_len) == 0;
}

static bool
inode_eq(void *x, void *y)
{
    inode_t *a = x;
    inode_t *b = y;
    return *a == *b;
}

static void
icache_entry_evict(void *k, void *v)
{
    ikey_t *kk = k;
    free(kk->file);
}

static void
icache_init(void)
{
    lru_init(&cache, ICACHE_ENTRIES, sizeof(ikey_t), ikey_eq,
             sizeof(inode_t), inode_eq, icache_entry_evict);
}
SET_ENTRY(init_hooks, icache_init);
