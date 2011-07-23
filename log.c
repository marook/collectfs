/**
 * Logging functions - all logging is sent to the syslog.
 * 
 * Function names prefixed with trace_ only produce output
 * if the COLLECTFS_TRACE environment variable is set.
 * 
 * These functions are careful to save and restore errno.
 * 
 * Partly based on log.c in BBFS from
 * http://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/
 * but this code is a total rewrite, now uses the syslog 
 * and distinguishes between tracing and error-logging.
 * 
 * Copyright 2011, Michael Hamilton
 * GPL 3.0(GNU General Public License) - see COPYING file
 */
#include <fuse.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include <syslog.h>
#include <stdarg.h>

#include "log.h"

#define ENABLE_TRACE_ENV "COLLECTFS_TRACE"
#define SYSLOG_IDENT "clfs"

/*  macro to log fields in structs. */
#define TRACE_STRUCT(st, field, format) \
  trace_info("    " #field " = " format , st->field)

static int log_all = -1;
static int use_syslog = 1;

int is_tracing()
{
    if (log_all == -1) {
        log_all = getenv(ENABLE_TRACE_ENV) ? 1 : 0;
        if (log_all == 0) {
            syslog(LOG_NOTICE, "Full logging disabled.");
            syslog(LOG_NOTICE, "Set environment variable %s to 1 for full logging", ENABLE_TRACE_ENV);
        }
        if (log_all == 1) {
            syslog(LOG_NOTICE, "Full logging enabled by %s.", ENABLE_TRACE_ENV);
        }
    }
    return log_all;
}

void set_tracing(int setting)
{
    log_all = setting;
}

void set_use_syslog(int setting)
{
    use_syslog = setting;
}

void log_open()
{
    openlog(SYSLOG_IDENT, LOG_CONS | LOG_PID, LOG_LOCAL2);
    if (use_syslog) {
        fprintf(stderr, "Logging to the system log");
    }
}

void log_info(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    if (use_syslog) {
        vsyslog(LOG_INFO, format, ap);
    } else {
        vfprintf(stderr, format, ap);
        fputc('\n', stderr);
    }
}

int log_errno(const char *format, ...)
{
    int err = errno;
    va_list ap;
    va_start(ap, format);
    if (use_syslog) {
        syslog(LOG_CRIT, "Errno=%d %s - ", err, strerror(err));
        vsyslog(LOG_CRIT, format, ap);
    } else {
        fprintf(stderr, "Errno=%d %s - ", err, strerror(err));
        fputc('\n', stderr);
        vfprintf(stderr, format, ap);
        fputc('\n', stderr);
    }
    errno = err;
    return errno;
}

void trace_info(const char *format, ...)
{
    if (!is_tracing()) {
        return;
    }
    va_list ap;
    va_start(ap, format);
    if (use_syslog) {
        vsyslog(LOG_INFO, format, ap);
    } else {
        vfprintf(stderr, format, ap);
        fputc('\n', stderr);
    }
}

int trace_errno(const char *format, ...)
{
    int err = errno;
    if (!is_tracing()) {
        errno = err;
        return errno;
    }
    va_list ap;
    va_start(ap, format);

    if (use_syslog) {
        syslog(LOG_ERR, "    >>>Errno=%d %s - ", err, strerror(err));
        vsyslog(LOG_ERR, format, ap);
    } else {
        fprintf(stderr, "    >>>>Errno=%d %s - ", err, strerror(err));
        fputc('\n', stderr);
        vfprintf(stderr, format, ap);
        fputc('\n', stderr);
    }
    errno = err;
    return errno;
}

/*+ struct fuse_file_info keeps information about files (surprise!). 
 *+ This dumps all the information in a struct fuse_file_info.  The struct 
 *+ definition, and comments, come from /usr/include/fuse/fuse_common.h 
 *+ Duplicated here for convenience. 
 */
void trace_fi(struct fuse_file_info *fi)
{
    if (!is_tracing()) {
        return;
    }
    /** Open flags.  Available in open() and release() */
    /*        int flags; */
    TRACE_STRUCT(fi, flags, "0x%08x");

    /** Old file handle, don't use */
    /*        unsigned long fh_old;         */
    TRACE_STRUCT(fi, fh_old, "0x%08lx");

    /** In case of a write operation indicates if this was caused by a
        writepage */
    /*        int writepage; */
    TRACE_STRUCT(fi, writepage, "%d");

    /** Can be filled in by open, to use direct I/O on this file.
        Introduced in version 2.4 */
    /*        unsigned int keep_cache : 1; */
    TRACE_STRUCT(fi, direct_io, "%d");

    /** Can be filled in by open, to indicate, that cached file data
        need not be invalidated.  Introduced in version 2.4 */
    /*        unsigned int flush : 1; */
    TRACE_STRUCT(fi, keep_cache, "%d");

    /** Padding.  Do not use*/
    /*        unsigned int padding : 29; */

    /** File handle.  May be filled in by filesystem in open().
        Available in all other file operations */
    /*        uint64_t fh; */
    TRACE_STRUCT(fi, fh, "0x%016llx");

    /** Lock owner id.  Available in locking operations and flush */
    /*  uint64_t lock_owner; */
    TRACE_STRUCT(fi, lock_owner, "0x%016llx");
};

/*+ This dumps the info from a struct stat.  The struct is defined in 
 *+ <bits/stat.h>; this is indirectly included from <fcntl.h> 
 */
void trace_stat(struct stat *si)
{
    if (!is_tracing()) {
        return;
    }
    /*  dev_t     st_dev;     // ID of device containing file */
    TRACE_STRUCT(si, st_dev, "%lld");

    /*  ino_t     st_ino;     // inode number */
    TRACE_STRUCT(si, st_ino, "%lld");

    /*  mode_t    st_mode;    // protection */
    TRACE_STRUCT(si, st_mode, "0%o");

    /*  nlink_t   st_nlink;   // number of hard links */
    TRACE_STRUCT(si, st_nlink, "%d");

    /*  uid_t     st_uid;     // user ID of owner */
    TRACE_STRUCT(si, st_uid, "%d");

    /*  gid_t     st_gid;     // group ID of owner */
    TRACE_STRUCT(si, st_gid, "%d");

    /*  dev_t     st_rdev;    // device ID (if special file) */
    TRACE_STRUCT(si, st_rdev, "%lld");

    /*  off_t     st_size;    // total size, in bytes */
    TRACE_STRUCT(si, st_size, "%lld");

    /*  blksize_t st_blksize; // blocksize for filesystem I/O */
    TRACE_STRUCT(si, st_blksize, "%ld");

    /*  blkcnt_t  st_blocks;  // number of blocks allocated */
    TRACE_STRUCT(si, st_blocks, "%lld");

    /*  time_t    st_atime;   // time of last access */
    TRACE_STRUCT(si, st_atime, "0x%08lx");

    /*  time_t    st_mtime;   // time of last modification */
    TRACE_STRUCT(si, st_mtime, "0x%08lx");

    /*  time_t    st_ctime;   // time of last status change */
    TRACE_STRUCT(si, st_ctime, "0x%08lx");

}

void trace_statvfs(struct statvfs *sv)
{
    if (!is_tracing()) {
        return;
    }
    /*  unsigned long  f_bsize;    // file system block size */
    TRACE_STRUCT(sv, f_bsize, "%ld");

    /*  unsigned long  f_frsize;   // fragment size */
    TRACE_STRUCT(sv, f_frsize, "%ld");

    /*  fsblkcnt_t     f_blocks;   // size of fs in f_frsize units */
    TRACE_STRUCT(sv, f_blocks, "%lld");

    /*  fsblkcnt_t     f_bfree;    // # free blocks */
    TRACE_STRUCT(sv, f_bfree, "%lld");

    /*  fsblkcnt_t     f_bavail;   // # free blocks for non-root */
    TRACE_STRUCT(sv, f_bavail, "%lld");

    /*  fsfilcnt_t     f_files;    // # inodes */
    TRACE_STRUCT(sv, f_files, "%lld");

    /*  fsfilcnt_t     f_ffree;    // # free inodes */
    TRACE_STRUCT(sv, f_ffree, "%lld");

    /*  fsfilcnt_t     f_favail;   // # free inodes for non-root */
    TRACE_STRUCT(sv, f_favail, "%lld");

    /*  unsigned long  f_fsid;     // file system ID */
    TRACE_STRUCT(sv, f_fsid, "%ld");

    /*  unsigned long  f_flag;     // mount flags */
    TRACE_STRUCT(sv, f_flag, "0x%08lx");

    /*  unsigned long  f_namemax;  // maximum filename length */
    TRACE_STRUCT(sv, f_namemax, "%ld");

}

void trace_utime(struct utimbuf *buf)
{
    if (!is_tracing()) {
        return;
    }
    /*    time_t actime; */
    TRACE_STRUCT(buf, actime, "0x%08lx");

    /*    time_t modtime; */
    TRACE_STRUCT(buf, modtime, "0x%08lx");
}
