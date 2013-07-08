#define FUSE_USE_VERSION 26
#define _GNU_SOURCE
#include <fuse.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <mysql/mysql.h>
#include <pthread.h>
#include <unistd.h>

#include "jimcore/sys_linker_set.h"
#include "jimcore/logging.h"
#include "jimcore/ring.h"
#include "jimcore/counter.h"
#include "jimcore/histogram.h"
#include "jimcore/lru.h"
#include "jimcore/connection.h"
#include "jimcore/mysql_helpers.h"

#define INVALID_INODE (0)
/* XXX */
#define ROOT_INODE (1)

typedef unsigned long long inode_t;

int dogfs_create(const char *path, mode_t mode, struct fuse_file_info *fi);
int dogfs_getattr(const char *path, struct stat *stbuf);
int dogfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                  off_t offset, struct fuse_file_info *fi);
int dogfs_open(const char *path, struct fuse_file_info *fi);
int dogfs_read(const char *path, char *buf, size_t len, off_t off,
               struct fuse_file_info *fi);
int dogfs_release(const char *path, struct fuse_file_info *fi);
int dogfs_truncate(const char *path, off_t newsize);
int dogfs_ftruncate(const char *path, off_t newsize,
                    struct fuse_file_info *fi);
int dogfs_utimens(const char *path, const struct timespec tv[2]);
int dogfs_unlink(const char *path);
int dogfs_rmdir(const char *path);
int dogfs_rename(const char *from, const char *to);
int dogfs_chmod(const char *path, mode_t mode);
int dogfs_chown(const char *path, uid_t uid, gid_t gid);
int dogfs_write(const char *path, const char *buf, size_t len, off_t off,
                struct fuse_file_info *fi);
int dogfs_mkdir(const char *path, mode_t mode);
int dogfs_symlink(const char *target, const char *source);
int dogfs_readlink(const char *path, char *buf, size_t len);
int dogfs_fsync(const char *path, int datasync, struct fuse_file_info *fi);
int dogfs_flush(const char *path, struct fuse_file_info *fi);
int dogfs_fgetattr(const char *path, struct stat *stbuf,
                   struct fuse_file_info *fi);



#include "file.h"

connection_t * connection_acquire(void);
void connection_release(connection_t *c);

int inode_unlink_trx(connection_t *c, inode_t dir, inode_t i, char *basename);
int inode_rmdir_trx(connection_t *c, inode_t dir, inode_t i, char *basename);
int inode_create(connection_t *c, inode_t dir, char *basename, mode_t mode,
                 inode_t *inode);
int inode_read(connection_t *c, inode_t inode, char *buf, size_t len,
               off_t off, bool pad);
int inode_write(connection_t *c, inode_t inode, const char *buf, size_t len,
                off_t off);


inode_t resolve_inode(connection_t *c, const char *path);
inode_t resolve_inode_from(connection_t *c, const char *path, inode_t from);
inode_t resolve_dir_inode(connection_t *c, const char *path, char **basename);
int inode_getattr(connection_t *c, inode_t inode, struct stat *stbuf);

/* inode cache */
inode_t icache_lookup(char *file, size_t len, inode_t from);
void icache_add(char *file, size_t len, inode_t from, inode_t inode);
void icache_remove(inode_t inode);

/* attribute cache */
int acache_get(connection_t *c, inode_t inode, struct stat *statbuf);
void acache_remove(inode_t inode);

/* errno for possibly transient connection / database issues */
#define ETRANSIENT EAGAIN
/* errno for possible dogfs2 bugs */
#define EINTERNAL EINVAL

/* XXX - read from configuration */
#define BLOCKSIZE (4096)

typedef void (*debug_hook)(void);
SET_DECLARE(debug_hooks, debug_hook);

typedef void (*init_hook)(void);
SET_DECLARE(init_hooks, init_hook);

#ifndef MIN
#define MIN(a,b)                \
    ({ typeof(a) _a = (a);      \
       typeof(b) _b = (b);      \
       _a < _b ? _a : _b; })
#endif
#ifndef MAX
#define MAX(a,b)                \
    ({ typeof(a) _a = (a);      \
       typeof(b) _b = (b);      \
       _a > _b ? _a : _b; })
#endif

static inline void
wallclock(struct timespec *tv)
{
    clock_gettime(CLOCK_REALTIME, tv);
}

static inline void
cache_remove(inode_t i)
{
    icache_remove(i);
    acache_remove(i);
}

static inline void
_spin_unlock_cleanup(pthread_spinlock_t **lockptr)
{
    pthread_spin_unlock(*lockptr);
}

#define SPIN_LOCK_FOR_SCOPE(_lock)                              \
    pthread_spinlock_t *__locked                                \
    __attribute__((cleanup(_spin_unlock_cleanup))) =            \
         (pthread_spin_lock(&(_lock)), &(_lock))

static inline void
_mutex_unlock_cleanup(pthread_mutex_t **lockptr)
{
    pthread_mutex_unlock(*lockptr);
}

#define MUTEX_LOCK_FOR_SCOPE(_lock)                               \
    pthread_mutex_t *__locked                                     \
    __attribute__((cleanup(_mutex_unlock_cleanup))) =             \
         (pthread_mutex_lock(&(_lock)), &(_lock))

#define LOOP_DECLARE(typ, i...) \
    for (typ *__decl_var = (void *)1, i; __decl_var; __decl_var = NULL)


static inline void
mutex_init(pthread_mutex_t *lock)
{
    static pthread_mutex_t factory = PTHREAD_MUTEX_INITIALIZER;
    *lock = factory;
}

static inline void
thread_create(pthread_t *t, void *(*thread_fn)(void *), void *arg)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_create(t, &attr, thread_fn, arg);
}

static inline void
bind_inode(MYSQL_BIND *bind, inode_t *val)
{
    memset(bind, 0, sizeof(*bind));
    bind->buffer = val;
    bind->buffer_type = MYSQL_TYPE_LONGLONG;
    bind->is_unsigned = true;
}

#define run_with_inode(__c, __p, func, args...)                         \
    ({                                                                  \
        inode_t _i = resolve_inode((__c), (__p));                       \
        int _r;                                                         \
        if (_i == INVALID_INODE) {                                      \
            _r = -ENOENT;                                               \
        } else {                                                        \
            _r = func(__c, _i, ##args);                                 \
            if (_r == -ENOENT)                                          \
                /* Maybe our cache is out of date. */                   \
                cache_remove(_i);                                       \
        }                                                               \
        _r;                                                             \
    })

#define run_with_c_inode(__p, func, args...)                    \
    ({                                                          \
        auto int _connection(connection_t *_c);                 \
        int _connection(connection_t *_c)                       \
        {                                                       \
            return run_with_inode(_c, (__p), func, ##args);     \
        }                                                       \
        run_with_connection(_connection);                       \
    })

#define run_with_c_inode_updating(__p, func, args ...)          \
    ({                                                          \
        auto int _inode(connection_t *_c, inode_t _i);          \
        int _inode(connection_t *_c, inode_t _i)                \
        {                                                       \
            int _r = func(_c, _i, ##args);                      \
            if (_r >= 0)                                        \
                acache_remove(_i);                              \
            return _r;                                          \
        }                                                       \
        auto int _connection(connection_t *_c);                 \
        int _connection(connection_t *_c)                       \
        {                                                       \
            return run_with_inode(_c, (__p), _inode);           \
        }                                                       \
        run_with_connection(_connection);                       \
    })
