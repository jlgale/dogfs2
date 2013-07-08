#include "dogfs.h"
#include <getopt.h>

#define FUSE_ARG_MAX (256)

static void * dogfs_init(struct fuse_conn_info *conn);

static struct fuse_operations dogfs_ops = {
    .chmod = dogfs_chmod,
    .chown = dogfs_chown,
    .create = dogfs_create,
    .flush = dogfs_flush,
    .fgetattr = dogfs_fgetattr,
    .fsync = dogfs_fsync,
    .ftruncate = dogfs_ftruncate,
    .getattr = dogfs_getattr,
    .init = dogfs_init,
    .mkdir = dogfs_mkdir,
    .open = dogfs_open,
    .readdir = dogfs_readdir,
    .read = dogfs_read,
    .readlink = dogfs_readlink,
    .release = dogfs_release,
    .rename = dogfs_rename,
    .rmdir = dogfs_rmdir,
    .symlink = dogfs_symlink,
    .truncate = dogfs_truncate,
    .unlink = dogfs_unlink,
    .utimens = dogfs_utimens,
    .write = dogfs_write,
};

static void
usage()
{
    char *text[] = {
        "dogfs2 [db options] [fuse options] mountpoint",
        "",
        "Userspace driver for the dogfood filesystem, a filesystem",
        "backed by a Clustrix database.",
        "",
        "Database options:",
        " -H hostname - database host to connect to [default dogfood]",
        " -S socket   - unix domain socket to connect to",
        " -u username - database username to connect with [default dogfs]",
        " -p          - ask for a connection password",
        " -D dbname   - database name to access [default dogfs]",
        "",
        "Fuse options:",
        " -s     - single threaded",
        " -V     - print fuse version and exit",
        " -f     - forground operation",
        " -d     - debug mode (implies -f)",
        " -o opt - fuse filesystem options",
        0
    };
    for (int i = 0; text[i]; ++i)
        printf("%s\n", text[i]);
}

static void
push_arg(int *argc, char **argv, char *arg)
{
    if (*argc >= FUSE_ARG_MAX)
        fatal("too many fuse arguments");
    argv[*argc] = arg;
    *argc = *argc + 1;
}

/* We're lazy and just pass the arguments to fuse to interpret, but along
 * the way we sniff them to adjust logging, &ct appropriately.
 */
static void
parse_commandline(int argc, char **argv,
                  int *fuse_argc_out, char ***fuse_argv_out)
{
    argv0 = strdup(basename(argv[0]));
    struct option long_opts[] = {
        {"version", 0, 0, 'V'},
        {"help", 0, 0, 'h'},
        {0, 0, 0, 0},
    };
    int fuse_argc = 0;
    char **fuse_argv = malloc(sizeof(char*) * FUSE_ARG_MAX);

#define PUSH_ARG(_s)                            \
    push_arg(&fuse_argc, fuse_argv, _s)

    PUSH_ARG(argv[0]);

    int opt;
    while ((opt = getopt_long(argc, argv, "dD:fhH:so:pu:V",
                              long_opts, NULL)) != -1) {
        switch (opt) {
        case 'd':
            debug = true;
            log_level = LOG_LEVEL_DEBUG;
            daemonized = false;
            PUSH_ARG("-d");
            break;
        case 'f':
            daemonized = false;
            PUSH_ARG("-f");
            break;
        case 'H':
            host = optarg;
            break;
        case 'u':
            user = optarg;
            break;
        case 'p':
            passwd = getpass("Password: ");
            if (!passwd)
                fatal("please provide a database password");
            break;
        case 'D':
            db = optarg;
            break;
        case 'S':
            unix_socket = optarg;
            break;
        case 'h':
            usage();
            exit(0);
            break;
        case 's':
            PUSH_ARG("-s");
            break;
        case 'V':
            PUSH_ARG("-V");
            break;
        case 'o':
            PUSH_ARG("-o");
            PUSH_ARG(optarg);
            break;
        case '?':
            exit(1);
        default:
            assert(0);
        }
    }

    static char *static_opts[] = {
        "-o", "big_writes",
        "-o", "default_permissions",
    };
    int nstatic_opts = sizeof(static_opts) / sizeof(char*);
    for (int i = 0; i < nstatic_opts; ++i)
        PUSH_ARG(static_opts[i]);

    for (int i = optind; i < argc; ++i)
        PUSH_ARG(argv[i]);

#undef PUSH_ARG
    *fuse_argc_out = fuse_argc;
    *fuse_argv_out = fuse_argv;
}

static void *
dogfs_init(struct fuse_conn_info *conn)
{
    init_hook **hptr;
    SET_FOREACH(hptr, init_hooks) {
        init_hook *x = *hptr;
        init_hook h = (init_hook)x;
        h();
    }
    return NULL;
}

int
main(int argc, char **argv)
{
    int fuse_argc;
    char **fuse_argv;
    parse_commandline(argc, argv, &fuse_argc, &fuse_argv);
    return fuse_main(fuse_argc, fuse_argv, &dogfs_ops, NULL /* user_data */);
}
