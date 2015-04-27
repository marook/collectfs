# Collectfs #

The purpose of collectfs is to protect a project hierarchy by providing a fairly universal no-clobber mechanism:

  * The history of changes is preserved.
  * Missteps in using rm, mv, cat, etc are non-permanent.
  * It works seamlessly with standard development tools.

Any file that is overwritten by remove (unlink), move, link, symlink, or open-truncate is relocated to a trash directory (mount-point/.trash/).  Removed files  are  date-time  stamped so that edit  history  is maintained (a version number is appended if the same file is collected more than once in the same second).

Usage is quite straight forward, for example:
```
% collectfs myProject myWorkspace
% cd myWorkspace
% vi main.c
% indent main.c
% ls .trash
main.c.2011-07-24.14:59:39
% diff .trash/main.c.2011-07-24.14:59:39 main.c
...
% mv .trash/main.c.2011-07-24.14:59:39 main.c
ls .trash
main.c.2011-07-24.15:00:37
...
cd ..
fusermount -u myWorkspace
```

Collectfs is intended as a light weight way to preserve changes made throughout the working day. It is not intended as a replacement for revision control or backups.
The intention is to protect you during the between-times, when you're not covered
by these other tools: those moments where you misstep with rm, mv, or cat; or those
times when you wish you could revert or compare your source to as it was an hour ago.

To provide some context for recovery, the trash directory hierarchy mirrors
the original directory hierarchy.  Collectfs uses rename to collect files and
move them into the trash hierarchy - this is quite efficient because it requires
no data to be copied.  Because collectfs relies on rename, the trash directory
must reside within the hierarchy being collected (i.e. the same physical filesystem).

Collectfs is currently aimed at preserving files and their associated directory
path as context. Only normal files are collected.  Symbolic links are collected, but
only as links.  Fifos are not collected.  Directories are only collected
if necessary to preserve file content - there is no protection for
empty directories.

The trash directory is visible within the collectfs. If you perform
operations such as rm on files inside a collectfs trash directory,
collectfs will just move them to a trash directory inside the trash
directory. For example, rm -r on the trash folder just results in a
trash folder inside the trash folder as well as a "Directory not empty"
error from rm command.  Note, you can really remove files from the
trash by using the real non-fuse path to the trash folder (collectfs
only protects you from operations performed within path under the
mount point).

Collectfs does not preserve directory permissions when duplicating directory
hierarchies.  Directories under the trash folder will be created with permissions
set to 0700 (user=rwx,group=none,others=none).  Because of this, files moved to
the trash folder may become less accessible to other users than before they
were moved.

There may be bugs that permanently destroy files.  I've been using collectfs
to create collectfs.  Other than this, testing has been limited.  As a
development tool it works for me, I also backup my system each day.  You
need to make your own assessment of how much trust you want to place in
collectfs and what measures you should take to protect yourself from any bugs
that might be present.