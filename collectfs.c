/*
 * Collect Filesystem - collect any files that get clobbered
 *
 * Copyright 2011, Michael Hamilton
 * GPL 3.0(GNU General Public License) - see COPYING file
 * 
 * Most of this code is loosly based on BBFS from the 
 * fuse tutorial - http://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/
 */

/*+ Comments with a *+ are from the bbfs fuse tutorial
 */

/*+ need this to get pwrite(). I have to use setvbuf() instead of
 *+ setlinebuf() later in consequence.
 */
#define _XOPEN_SOURCE 500

#include <limits.h>
#include <dirent.h>
#include <errno.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <unistd.h>



#include <sys/types.h>
#include <sys/xattr.h>

/*+ The FUSE API has been changed a number of times. So, our code
 *+ needs to define the version of the API that we assume. As of this
 *+ writing, the most current API version is 26
 */
#define FUSE_USE_VERSION 26
#include <fuse.h>

#include "log.h"

/**
 * Will show up in logs
 */
#define COLLECTFS_VERSION "1.0.0"

/**
 * Collect() function return values - different operations
 * may need to know what collect did - e.g. when unlinking,
 * if collect moved the file to the trash, then unlink will
 * have nothing to do. 
 */
/**
 * Collected the file - must have been a normal file
 */
#define COLLECT_COLLECTED 0
/**
 * Asked to collect something that isn't a normal file, 
 * e.g. a named pipe.
 */
#define COLLECT_NOT_COLLECTABLE 1
/**
 * Asked to collect a file that doesn't exist.
 */
#define COLLECT_DOES_NOT_EXIST 2
/**
 * A real collection problem, such as file name to long
 * or an error in the underlying real file-system.
 */
#define COLLECT_ERROR -1


/**
 * We will pass this context to fuse.  Fuse will pass it back
 * to us - so we can attach any context we need here.
 * Currently we just need to know the real root of the directory
 * hierarchy we are protecting.  
 */
struct local_context {
    char *rootdir;
};

/**
 * Only available on more recent kernels
 */
static int can_collect_open_truncate = 0;

static int help_only = 0;

static char *trashname = ".trash";

static int fop_create(const char *path, mode_t mode, struct fuse_file_info *fi);

/**
 * An enumeration to generate the values for keys in the command line 
 * options structure 
 */
enum {
    ID_HELP,
    ID_FUSE_HELP,
    ID_VERSION,
    ID_TRACE,
    ID_MONITOR,
    ID_CENSOR,
};

/** 
 * Command line options definitions for passing to fuse so fuse options
 * and our options become unified.
 */
static struct fuse_opt command_options[] = {
    FUSE_OPT_KEY("-h",          ID_HELP),
    FUSE_OPT_KEY("--help",      ID_HELP),
    FUSE_OPT_KEY("-H",          ID_FUSE_HELP),
    FUSE_OPT_KEY("--help-fuse", ID_FUSE_HELP),
    FUSE_OPT_KEY("-H",          ID_FUSE_HELP),
    FUSE_OPT_KEY("--help-fuse", ID_FUSE_HELP),
    FUSE_OPT_KEY("-V",          ID_VERSION),
    FUSE_OPT_KEY("--version",   ID_VERSION),
    FUSE_OPT_KEY("-t",          ID_TRACE),
    FUSE_OPT_KEY("--trace",     ID_TRACE),
    FUSE_OPT_KEY("-f",          ID_MONITOR),
    FUSE_OPT_KEY("-xxxxx",      ID_CENSOR), /* Not for fuse to see - to be removed */
    FUSE_OPT_END
};

static void usage(char *prog)
{
    fprintf(stderr,
            "\nCollectfs (version %s)\n\n"
            "Usage: [options|fuse-options] %s rootDir mountPoint\n\n"
            "Options:\n"
            "   -h, --help            collectfs help\n"
            "   -H, --help-fuse       fuse help\n"
            "   -V, --version         collectfs version\n"
            "   -t, --trace           log all file operations\n"
            "   -f                    run in foreground and log to stderr\n\n"
            "Environment variables:\n"
            "   COLLECTFS_LOGALL      if set, log all filesystem operations.\n"
            "   COLLECTFS_TRASH       the trash folder name (%s)\n\n", COLLECTFS_VERSION, prog, trashname);
}

static int command_options_processor(void *data, const char *arg, int key, struct fuse_args *outargs)
{   
    /* Return -1 to indicate error, 0 to accept parameter,
     * 1 to retain parameter and pase to FUSE
     */
    switch (key) {
    case ID_FUSE_HELP:
        fuse_opt_add_arg(outargs, "-ho");       /* add to the args that FUSE will see */
        return 0;
    case ID_HELP:
        help_only = 1;
        return 0;
    case ID_VERSION:
        printf("collectfs version: %s\n", COLLECTFS_VERSION);
        exit(0); /* sometimes its best to take the easy road */
        return 1;
    case ID_TRACE:
        set_tracing(1);
        return 0;
    case ID_MONITOR:
        /* force foreground operation */
        set_use_syslog(0);
        return 1;
    case ID_CENSOR:
        /* remove any arg/parameter we don't want fuse to see. */
        return 0;

    default:
        /* if we don't recognise it assume it is for fuse_main */
        return 1;
    }
}

/**
 * The fpath canonical root of the filesystem will be retrieved 
 * and passed from the context we pass to fuse.  Fuse will pass a 
 * file path relative to the root.  We can combine the two to 
 * return the full canonical path to a file. 
 */
static int get_fullpath(char fpath[PATH_MAX], const char *path)
{
    struct local_context *mycontext = (struct local_context *)fuse_get_context()->private_data;

    if (strlen(fpath) + strlen(path) >= PATH_MAX) {
        errno = ENAMETOOLONG;
        return log_errno("Full path too long: '%s%s'", fpath, path);
    }
    strcpy(fpath, mycontext->rootdir);
    strcat(fpath, path);

    trace_info(LOG_INDENT("get_fullpath:  rootdir = '%s', path = '%s', fpath = '%s'"), mycontext->rootdir, path, fpath);
    return 0;
}

/**
 * Check return status values - if OK, return the return status value.
 * If negative, return the negated errno instead of the return status - 
 * fuse will detect the negative errno and set the actual errno seen 
 * in user space.
 */
static int wrap_op(const char *opname, const int return_status)
{
    if (return_status < 0) {
        if (opname != NULL) {
            trace_errno(LOG_INDENT("%s"), opname);
        }
        /* fuse wants the negated error number returned on error */
        return -errno;
    }
    /* otherwise return the actual return value */
    return return_status;
}

/** 
 * Replicate the original path for the file being trashed
 * rooted in the trash folder. 
 * 
 * Based on code from GNU mkdir with the -p option.
 */
static int mkdir_trash_path(char *fspath)
{
    /* TODO copy permissions from the original path */
    int parent_mode = 0700;
    struct stat sb;
    char *p;

    char npath[PATH_MAX];

    trace_info(LOG_INDENT("make path=%s"), fspath);

    strcpy(npath, fspath);      /* So we can write to it. */

    /* Check whether or not we need to do anything with intermediate dirs. */

    /* Skip leading slashes. */
    p = npath;
    while (*p == '/') {
        p++;
    }

    while ((p = strchr(p, '/'))) {
        *p = '\0';
        if (stat(npath, &sb) != 0) {
            if (mkdir(npath, parent_mode)) {
                log_errno("Cannot create directory: '%s'", npath);
                return -1;
            }
        } else if (S_ISDIR(sb.st_mode) == 0) {
            errno = ENOTDIR;
            log_errno("File exists but is not a directory: '%s'", npath);
            return -1;
        }

        *p++ = '/';             /* restore slash */
        while (*p == '/') {
            p++;
        }
    }

    return 0;
}

/** 
 * Move a file to the trash (archive folder).
 * 
 * Called when a file is being unlinked, open-truncate,
 * or overwritten by move, link or symlink.
 * Sets errno on error.
 */
static int collect(const char *path, mode_t * mode)
{
    int rstatus = 0;
    char fpath[PATH_MAX];

    trace_info(LOG_INDENT("collect(path='%s')"), path);
    if (get_fullpath(fpath, path) != 0) {
        log_errno("Full path name too long to collect %s", path);
        return COLLECT_ERROR;
    }

    struct stat statbuf;

    if (stat(fpath, &statbuf) == -1) {
        trace_errno("OK - no file to collect (stat failed) path=%s fpath=%s", path, fpath);
        return COLLECT_DOES_NOT_EXIST;
    }

    if (mode != NULL) {
        *mode = statbuf.st_mode;
    }

    if (!S_ISREG(statbuf.st_mode)) {
        /* Only collect regular files. */
        return COLLECT_NOT_COLLECTABLE;
    }

    time_t now = time(NULL);

    struct tm *tmp = localtime(&now);
    if (tmp == NULL) {
        log_errno("failed to obtain localtime");
        return COLLECT_ERROR;
    }
    char time_suffix[strlen("-YYYY-MM-DD.HH:MM:SS") + 1];
    if (strftime(time_suffix, sizeof(time_suffix), ".%Y-%m-%d.%H:%M:%S", tmp) == 0) {
        log_errno("strftime returned 0");
        return COLLECT_ERROR;
    }
    char trashpath[PATH_MAX];
    char trashfolder[strlen(trashname) + 1];
    strcpy(trashfolder, "/");
    strcat(trashfolder, trashname);
    if (get_fullpath(trashpath, trashfolder) != 0) {
        log_errno("Trash folder path too long %s%s", trashpath, trashfolder);
        return COLLECT_ERROR;
    }
    if (strlen(trashpath) + strlen(path) >= PATH_MAX) {
        errno = ENAMETOOLONG;
        log_errno("Path too long to use trash %s%s", trashpath, path);
        return COLLECT_ERROR;
    }
    strcat(trashpath, path);
    if (mkdir_trash_path(trashpath) != 0) {
        /* errno will have been set and logged */
        return COLLECT_ERROR;
    }
    char fnewpath[PATH_MAX];
    char suffix[15] = "";       /* will fit maxint */
    int i = 0;
    for (i = 1;; i++) {         /* If date-time is not unique, add a counter */

        if (strlen(trashpath) + strlen(time_suffix) + strlen(suffix) > PATH_MAX) {
            errno = ENAMETOOLONG;
            log_errno("Path too long to use trash %s", path);
            return COLLECT_ERROR;
        }

        strcpy(fnewpath, trashpath);
        strcat(fnewpath, time_suffix);
        strcat(fnewpath, suffix);
        struct stat sb;
        if (stat(fnewpath, &sb) != 0) {
            break;
        }
        sprintf(suffix, "-%04d", i);
    }

    rstatus = rename(fpath, fnewpath);
    if (rstatus < 0) {
        log_errno("collect rename %s", path);
        return COLLECT_ERROR;
    }

    return COLLECT_COLLECTED;
}

static int fop_getattr(const char *path, struct stat *statbuf)
{
    int rstatus = 0;
    char fpath[PATH_MAX];

    trace_info("fop_getattr(path='%s', statbuf=0x%08x)", path, statbuf);
    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };

    rstatus = wrap_op("fop_getattr (lstat)", lstat(fpath, statbuf));
    trace_stat(statbuf);

    return rstatus;
}

/*+ Note the system readlink() will truncate and lose the terminating
 *+ null. So, the size passed to to the system readlink() must be one
 *+ less than the size passed to fop_readlink()
 *+ fop_readlink() code by Bernardo F Costa (thanks!)
 */
static int fop_readlink(const char *path, char *link, size_t size)
{
    int rstatus = 0;
    char fpath[PATH_MAX];

    trace_info("fop_readlink(path='%s', link='%s', size=%d)", path, link, size);
    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };

    rstatus = wrap_op("fop_readlink", readlink(fpath, link, size - 1));
    if (rstatus >= 0) {
        link[rstatus] = '\0';
        rstatus = 0;
    }

    return rstatus;
}

static int fop_mknod(const char *path, mode_t mode, dev_t dev)
{
    int rstatus = 0;
    char fpath[PATH_MAX];

    trace_info("fop_mknod(path='%s', mode=0%3o, dev=%lld)", path, mode, dev);
    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };

/*+ On Linux this could just be 'mknod(path, mode, rdev)' but this
 *+ is more portable
 */
    if (S_ISREG(mode)) {
        rstatus = wrap_op("fop_mknod (open)", open(fpath, O_CREAT | O_EXCL | O_WRONLY, mode));
        if (rstatus >= 0) {
            rstatus = wrap_op("fop_mknod (close)", close(rstatus));
        }
    } else if (S_ISFIFO(mode)) {
        rstatus = wrap_op("fop_mknod (mkfifo)", mkfifo(fpath, mode));
    } else {
        rstatus = wrap_op("fop_mknod (mknod)", mknod(fpath, mode, dev));
    }
    return rstatus;
}

static int fop_mkdir(const char *path, mode_t mode)
{
    char fpath[PATH_MAX];

    trace_info("fop_mkdir(path='%s', mode=0%3o)", path, mode);
    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };

    return wrap_op("fop_mkdir", mkdir(fpath, mode));
}

static int fop_unlink(const char *path)
{
    char fpath[PATH_MAX];

    trace_info("fop_unlink(path='%s')", path);

    /* Save the file being unlinked */
    int collected = collect(path, NULL);
    switch (collected) {
    case COLLECT_COLLECTED:
        /* Saved a file that would have been clobbered -
         * nothing left to delete, so return.
         */
        return 0;
    case COLLECT_DOES_NOT_EXIST:
        /* Unlinking something that is not there?
         * Think we'll just let unlink handle this.
         */
        /* fall thru */
    case COLLECT_ERROR:
        /* Don't allow the file to be unlinked, let the user
         * deal with this error
         */
        return -errno;
    case COLLECT_NOT_COLLECTABLE:
        /* Not collectible - eg a named pipe - let unlink do
         * its job.
         */
        break;
    }

    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };

    return wrap_op("fop_unlink", unlink(fpath));
}

static int fop_rmdir(const char *path)
{
    char fpath[PATH_MAX];

    trace_info("fop_rmdir(path='%s')", path);
    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };

    return wrap_op("fop_rmdir", rmdir(fpath));
}

static int fop_symlink(const char *path, const char *link)
{
    char flink[PATH_MAX];

    trace_info("fop_symlink(path='%s', link='%s')", path, link);

    int collected = collect(path, NULL);
    switch (collected) {
    case COLLECT_COLLECTED:
    case COLLECT_DOES_NOT_EXIST:
    case COLLECT_NOT_COLLECTABLE:
        /* Either we saved a file or we didn't need to
         */
        break;
    case COLLECT_ERROR:        /* Collect failed - do not complete the link op */
        return -errno;
    }

    if (get_fullpath(flink, link) != 0) {
        return -ENAMETOOLONG;
    };

    return wrap_op("fop_symlink", symlink(path, flink));
}

static int fop_rename(const char *path, const char *newpath)
{
    char fpath[PATH_MAX];
    char fnewpath[PATH_MAX];

    trace_info("fop_rename(fpath='%s', newpath='%s')", path, newpath);

    int collected = collect(newpath, NULL);
    switch (collected) {
    case COLLECT_COLLECTED:
    case COLLECT_DOES_NOT_EXIST:
    case COLLECT_NOT_COLLECTABLE:
        /* Either we saved a file or we didn't need to
         */
        break;
    case COLLECT_ERROR:
        return -errno;
    }

    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };
    if (get_fullpath(fnewpath, newpath) != 0) {
        return -ENAMETOOLONG;
    };

    return wrap_op("fop_rename", rename(fpath, fnewpath));
}

static int fop_link(const char *path, const char *newpath)
{
    char fpath[PATH_MAX], fnewpath[PATH_MAX];

    trace_info("fop_link(path='%s', newpath='%s')", path, newpath);
    
    /* Don't let a link clobber an existing file - can this happen? Lets be safe. */
    int collected = collect(newpath, NULL);
    switch (collected) {
    case COLLECT_COLLECTED:
    case COLLECT_DOES_NOT_EXIST:
    case COLLECT_NOT_COLLECTABLE:
        /* Either we saved a file or we didn't need to */
        break;
    case COLLECT_ERROR:
        return -errno;
    }

    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };
    if (get_fullpath(fnewpath, newpath) != 0) {
        return -ENAMETOOLONG;
    };

    return wrap_op("fop_link", link(fpath, fnewpath));
}

static int fop_chmod(const char *path, mode_t mode)
{
    char fpath[PATH_MAX];

    trace_info("fop_chmod(fpath='%s', mode=0%03o)", path, mode);
    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };

    return wrap_op("fop_chmod", chmod(fpath, mode));
}

static int fop_chown(const char *path, uid_t uid, gid_t gid)
{
    char fpath[PATH_MAX];

    trace_info("fop_chown(path='%s', uid=%d, gid=%d)", path, uid, gid);
    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };

    return wrap_op("fop_chown", chown(fpath, uid, gid));
}

static int fop_truncate(const char *path, off_t newsize)
{
    char fpath[PATH_MAX];

    trace_info("fop_truncate(path='%s', newsize=%lld)", path, newsize);

    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };

    return wrap_op("fop_truncate", truncate(fpath, newsize));
}

static int fop_utime(const char *path, struct utimbuf *ubuf)
{
    char fpath[PATH_MAX];

    trace_info("fop_utime(path='%s', ubuf=0x%08x)", path, ubuf);
    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };

    return wrap_op("fop_utime", utime(fpath, ubuf));
}

static int fop_open(const char *path, struct fuse_file_info *fi)
{
    int rstatus = 0;
    int fd;
    char fpath[PATH_MAX];

    trace_info("fop_open(path'%s', fi=0x%08x)", path, fi);

    if (can_collect_open_truncate && (fi->flags & O_TRUNC)) {
        /* If truncating an existing file, collect the existing file
         * and replace it with a new empty one.
         */
        mode_t mode;
        int collected = collect(path, &mode);
        switch (collected) {
        case COLLECT_COLLECTED:
            /* We have collected the file - replace it with new version
             * If this failed the following op will fail - so we don't
             * need to handle the error.
             */
            fop_create(path, mode, fi);
            break;
        case COLLECT_DOES_NOT_EXIST:
        case COLLECT_NOT_COLLECTABLE:
            break;
        case COLLECT_ERROR:
            return -errno;
        }
    }

    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };

    fd = wrap_op("fop_open", open(fpath, fi->flags));
    if (fd < 0) {
        rstatus = fd;
    }
    fi->fh = fd;
    trace_fi(fi);

    return rstatus;
}

static int fop_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    trace_info("fop_read(path='%s', buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)", path, buf, size, offset, fi);
    trace_fi(fi);

    return wrap_op("fop_read", pread(fi->fh, buf, size, offset));
}

static int fop_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    trace_info("fop_write(path='%s', buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)", path, buf, size, offset, fi);
    trace_fi(fi);

    return wrap_op("fop_write (pwrite)", pwrite(fi->fh, buf, size, offset));
}

static int fop_statfs(const char *path, struct statvfs *statv)
{
    int rstatus = 0;
    char fpath[PATH_MAX];

    trace_info("fop_statfs(path='%s', statv=0x%08x)", path, statv);
    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };

    rstatus = wrap_op("fop_statfs (statvfs)", statvfs(fpath, statv));
    trace_statvfs(statv);

    return rstatus;
}


static int fop_flush(const char *path, struct fuse_file_info *fi)
{
    trace_info("fop_flush(path='%s', fi=0x%08x)", path, fi);
    trace_fi(fi);
    /* based on symlinkfs from the fuse examples. */
    /* See if the file is still open */
    int fd = wrap_op("fop_flush (dup)",dup(fi->fh));
    if (fd < 0) {
        /* What to do now? It may be closed - try a fsync */
        if (wrap_op("fop_flush (fsync)", fsync(fi->fh)) < 0) {
            return -EIO; /* TODO Do we need to do this? */
        }
        return 0;
    }
    return wrap_op("fop_flush (close)", close(fd));
}

static int fop_release(const char *path, struct fuse_file_info *fi)
{
    trace_info("fop_release(path='%s', fi=0x%08x)", path, fi);
    trace_fi(fi);

    return wrap_op("fop_release (close)", close(fi->fh));
}

static int fop_fsync(const char *path, int datasync, struct fuse_file_info *fi)
{
    int rstatus = 0;

    trace_info("fop_fsync(path='%s', datasync=%d, fi=0x%08x)", path, datasync, fi);
    trace_fi(fi);

    if (datasync) {
        rstatus = wrap_op("fop_fsync (fdatasync)", fdatasync(fi->fh));
    } else {
        rstatus = wrap_op("fop_fsync (fsync)", fsync(fi->fh));
    }
    return rstatus;
}

static int fop_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
    char fpath[PATH_MAX];

    trace_info("fop_setxattr(path='%s', name='%s', value='%s', size=%d, flags=0x%08x)", path, name, value, size, flags);
    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };

    return wrap_op("fop_setxattr (lsetxattr)", lsetxattr(fpath, name, value, size, flags));
}

static int fop_getxattr(const char *path, const char *name, char *value, size_t size)
{
    int rstatus = 0;
    char fpath[PATH_MAX];

    trace_info("fop_getxattr(path = '%s', name = '%s', value = 0x%08x, size = %d)", path, name, value, size);
    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };

    rstatus = wrap_op("fop_getxattr (lgetxattr)", lgetxattr(fpath, name, value, size));
    if (rstatus >= 0) {
        trace_info(LOG_INDENT("value = '%s'"), value);
    }
    return rstatus;
}

static int fop_listxattr(const char *path, char *list, size_t size)
{
    int rstatus = 0;
    char fpath[PATH_MAX];
    char *ptr;

    trace_info("fop_listxattr(path='%s', list=0x%08x, size=%d)", path, list, size);
    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };

    rstatus = wrap_op("fop_listxattr (llistxattr)", llistxattr(fpath, list, size));
    trace_info(LOG_INDENT("returned attributes (length %d):"), rstatus);
    for (ptr = list; ptr < list + rstatus; ptr += strlen(ptr) + 1) {
        trace_info(LOG_INDENT("'%s'"), ptr);
    }
    return rstatus;
}

static int fop_removexattr(const char *path, const char *name)
{
    char fpath[PATH_MAX];

    trace_info("fop_removexattr(path='%s', name='%s')", path, name);
    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };

    return wrap_op("fop_rmovexattr (lremovexattr)", lremovexattr(fpath, name));
}

static int fop_opendir(const char *path, struct fuse_file_info *fi)
{
    DIR *dp;
    int rstatus = 0;
    char fpath[PATH_MAX];

    trace_info("fop_opendir(path='%s', fi=0x%08x)", path, fi);
    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };

    dp = opendir(fpath);
    if (dp == NULL) {
        /* fake call to record what what happened - errno will logged  */
        rstatus = wrap_op("fop_opendir (opendir)", -1);
    }
    fi->fh = (intptr_t) dp;

    trace_fi(fi);

    return rstatus;
}

static int fop_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    int rstatus = 0;
    DIR *dp;
    struct dirent *de;

    trace_info("fop_readdir(path='%s', buf=0x%08x, filler=0x%08x, offset=%lld, fi=0x%08x)", path, buf, filler, offset, fi);
    /*+ once again, no need for fullpath -- but note that I need to cast fi->fh
     */
    dp = (DIR *) (uintptr_t) fi->fh;

    /*+ Every directory contains at least two entries: . and .. If my
     *+ first call to the system readdir() returns NULL I've got an
     *+ error; near as I can tell, that's the only condition under
     *+ which I can get an error from readdir()
     */
    de = readdir(dp);
    if (de == 0) {
        /* record what op and what happened - errno will logged  */
        return wrap_op("fop_readdir (readdir)", -1);
    }
    /*+ This will copy the entire directory into the buffer. The loop exits
     *+ when either the system readdir() returns NULL, or filler()
     *+ returns something non-zero. The first case just means I've
     *+ read the whole directory; the second means the buffer is full.
     */
    do {
        trace_info(LOG_INDENT("calling filler with name %s"), de->d_name);
        if (filler(buf, de->d_name, NULL, 0) != 0) {
            trace_info(LOG_INDENT("ERROR fop_readdir filler:  buffer full"));
            return -ENOMEM;
        }
        errno = 0;
    }
    while ((de = readdir(dp)) != NULL);
    if (errno != 0) {
        /* record what op and what happened - errno will logged  */
        return wrap_op("fop_readdir (readdir) while reading entries", -1);
    }

    trace_fi(fi);

    return rstatus;
}

static int fop_releasedir(const char *path, struct fuse_file_info *fi)
{
    trace_info("fop_releasedir(path='%s', fi=0x%08x)", path, fi);
    trace_fi(fi);

    return wrap_op("fop_releasedir (closedir)", closedir((DIR *) (uintptr_t) fi->fh));
}

static int fop_fsyncdir(const char *path, int datasync, struct fuse_file_info *fi)
{
    int rstatus = 0;

    trace_info("fop_fsyncdir(path='%s', datasync=%d, fi=0x%08x)", path, datasync, fi);
    trace_fi(fi);
    /* TODO - how to test this one? */
    if (datasync) {
        rstatus = wrap_op("fop_fsyncdir (fdatasync)", fdatasync(fi->fh));
    } else {
        rstatus = wrap_op("fop_fsyncdir (fsync)", fsync(fi->fh));
    }
    return rstatus;
}

void *fop_init(struct fuse_conn_info *conn)
{
    struct local_context *mycontext = (struct local_context *)fuse_get_context()->private_data;
    
    log_info("Collectfs starting: [%s]", mycontext->rootdir);
    trace_info("fop_init()");
    if ((unsigned int)conn->capable & FUSE_CAP_ATOMIC_O_TRUNC) {
        /* We want open to handle open-truncate so we can collect the
         * file being replaced.
         */
        conn->want |= FUSE_CAP_ATOMIC_O_TRUNC;
        can_collect_open_truncate = 1;
        log_info("Collectfs %s: FUSE_CAP_ATOMIC_O_TRUNC is supported - will collect open truncate.", COLLECTFS_VERSION);
    } else {
        log_info("Collectfs %s: WARNING, cannot collect open truncate - not supported by this kernel.", COLLECTFS_VERSION);
    }

    return mycontext;
}

void fop_destroy(void *userdata)
{
    trace_info("fop_destroy(userdata=0x%08x)", userdata);
}

static int fop_access(const char *path, int mask)
{
    char fpath[PATH_MAX];

    trace_info("fop_access(path='%s', mask=0%o)", path, mask);
    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };

    return wrap_op("fop_access", access(fpath, mask));
}

static int fop_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    int rstatus = 0;
    char fpath[PATH_MAX];
    int fd;

    trace_info("fop_create(path='%s', mode=0%03o, fi=0x%08x)", path, mode, fi);

    if (get_fullpath(fpath, path) != 0) {
        return -ENAMETOOLONG;
    };

    fd = wrap_op("fop_create", creat(fpath, mode));
    if (fd < 0) {               /* return error status */
        rstatus = fd;
        fi->fh = -1;
    } else {                    /* return 0 for success */
        rstatus = 0;
        fi->fh = fd;
        trace_fi(fi);
    }
    return rstatus;
}

static int fop_ftruncate(const char *path, off_t offset, struct fuse_file_info *fi)
{
    trace_info("fop_ftruncate(path='%s', offset=%lld, fi=0x%08x)", path, offset, fi);
    trace_fi(fi);
    /* TODO - check - maybe we should check if offset is zero
     * and collect the file in that case only.
     */
    return wrap_op("fop_ftruncate", ftruncate(fi->fh, offset));
}

static int fop_fgetattr(const char *path, struct stat *statbuf, struct fuse_file_info *fi)
{
    int rstatus = 0;

    trace_info("fop_fgetattr(path='%s', statbuf=0x%08x, fi=0x%08x)", path, statbuf, fi);
    trace_fi(fi);

    rstatus = wrap_op("fop_fgetattr (fstat)", fstat(fi->fh, statbuf));
    trace_stat(statbuf);

    return rstatus;
}

struct fuse_operations fuse_ops = {
    .getattr = fop_getattr,
    .readlink = fop_readlink,
    .getdir = NULL /* deprecated */ ,
    .mknod = fop_mknod,
    .mkdir = fop_mkdir,
    .unlink = fop_unlink,
    .rmdir = fop_rmdir,
    .symlink = fop_symlink,
    .rename = fop_rename,
    .link = fop_link,
    .chmod = fop_chmod,
    .chown = fop_chown,
    .truncate = fop_truncate,
    .utime = fop_utime,
    .open = fop_open,
    .read = fop_read,
    .write = fop_write,
    .statfs = fop_statfs,
    .flush = fop_flush,
    .release = fop_release,
    .fsync = fop_fsync,
    .setxattr = fop_setxattr,
    .getxattr = fop_getxattr,
    .listxattr = fop_listxattr,
    .removexattr = fop_removexattr,
    .opendir = fop_opendir,
    .readdir = fop_readdir,
    .releasedir = fop_releasedir,
    .fsyncdir = fop_fsyncdir,
    .init = fop_init,
    .destroy = fop_destroy,
    .access = fop_access,
    .create = fop_create,
    .ftruncate = fop_ftruncate,
    .fgetattr = fop_fgetattr
};

int main(int argc, char *argv[])
{
    int rstatus=0;
    int param_index; /* first non option parameter */
    struct local_context *context;

    /* TODO - figure out what to do about this comment - from bbfs */
    /*+ collectfs doesn't do any access checking on its own (the comment
     *+ blocks in fuse.h mention some of the functions that need
     *+ accesses checked -- but note there are other functions, like
     *+ chown(), that also need checking!). Since running bbfs as root
     *+ will therefore open Metrodome-sized holes in the system
     *+ security, we'll check if root is trying to mount the filesystem
     *+ and refuse if it is. The somewhat smaller hole of an ordinary
     *+ user doing it with the allow_other flag is still there because
     */
    if ((getuid() == 0) || (geteuid() == 0)) {
        fprintf(stderr, "Running collectfs as root opens unnacceptable security holes.\n");
        return EXIT_FAILURE;
    }

    context = calloc(sizeof(struct local_context), 1);
    if (context == NULL) {
        perror("Failed to initialise - failed to allocate memory for internal context (via calloc).\n");
        return EXIT_FAILURE;
    }
    /* Find first argument that isn't an option - one that doesn't start with - */
    for (param_index = 1; (param_index < argc) && (argv[param_index][0] == '-'); param_index++) {
        if (argv[param_index][1] == 'o' && argv[param_index][2] == '\0') {
            param_index++;                /* Skip over -o arg */
        }
    }

    if (argc - param_index < 2) {
        fprintf(stderr, "Missing required rootdir and mountpoint.\n");
        for (; param_index < argc; param_index++) {
            argv[param_index] = "-xxxxx"; /* Make sure fuse doesn't get any non-option parameters and mount anything */
        }
        help_only = 1;
        rstatus = EXIT_FAILURE;
        /* we need to keep going because the reason there are no parameters
         * may be because the user is asking for fuse help 
         */
    } else {
        /* Extract the root dir argument - leave the rest for fuse to handle. */
        context->rootdir = realpath(argv[param_index], NULL);

        if (context->rootdir == NULL) {
            fprintf(stderr, "%s: root path %s\n", strerror(errno), argv[param_index]);
            return EXIT_FAILURE;
        } else {
            struct stat sb;
            if (stat(context->rootdir, &sb) != 0 || !S_ISDIR(sb.st_mode)) {
                fprintf(stderr, "Root path be a directory: %s\n", context->rootdir);
                return EXIT_FAILURE;
            }
            argv[param_index] = "-xxxxx";     /* Indicate to the option parser to remove this argument */
        }
        
        const char *mount_point = realpath(argv[param_index + 1], NULL);
        if (mount_point == NULL) {
            fprintf(stderr, "%s: mount point %s\n", strerror(errno), argv[param_index + 1]);
            return EXIT_FAILURE;
        } else {
            struct stat sb;
            if (stat(mount_point, &sb) != 0 || !S_ISDIR(sb.st_mode)) {
                fprintf(stderr, "Mount point be a directory: %s\n", mount_point);
                return EXIT_FAILURE;
            }
            if (strcmp(mount_point,context->rootdir) == 0) {
                fprintf(stderr, "Mount point and root path cannot be the same directory: %s\n", mount_point);
                return EXIT_FAILURE;
            }
        }

        if (getenv("COLLECTFS_TRASH") != NULL) {
            trashname = getenv("COLLECTFS_TRASH");
            if (strchr(trashname, '/') != NULL) {
                fprintf(stderr, "COLLECTFS_TRASH must be a directory at the filesystem root - a path is not allowed.\n");
                return EXIT_FAILURE;
            }
        }
        log_open();
        fprintf(stderr, "\nCollectfs %s (trash=%s)\n\n", COLLECTFS_VERSION, trashname);
    }

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    if (fuse_opt_parse(&args, NULL, command_options, command_options_processor) == 0) {
        rstatus = fuse_main(args.argc, args.argv, &fuse_ops, context);
    }
    if (help_only) {
        usage(argv[0]);
    }
    else {
        log_info("Collectfs exiting: [%s]", context->rootdir);
    }
    return rstatus;
}
