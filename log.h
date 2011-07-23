/**
 *  Copyright 2011, Michael Hamilton
 *  GPL 3.0(GNU General Public License) - see COPYING file
 */
#ifndef _LOG_H_
#define _LOG_H_
#include <stdio.h>
#include <errno.h>


#define LOG_INDENT(str) ("    " str)

void log_open();

void log_info(const char *format, ...);
int log_errno(const char *format, ...);

void set_tracing(int setting);
void set_use_syslog(int setting);
void trace_info(const char *format, ...);
int trace_errno(const char *format, ...);

void trace_fi (struct fuse_file_info *fi);
void trace_stat(struct stat *si);
void trace_statvfs(struct statvfs *sv);
void trace_utime(struct utimbuf *buf);

#endif
