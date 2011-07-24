#
# OpenSUSE file for package collectfs
#
# Copyright 2011, Michael Hamilton
# GPL 3.0(GNU General Public License) - see COPYING file
#
#
 
# norootforbuild
 
 
Name:           collectfs
BuildRequires:  fuse-devel gcc-c++ pkgconfig 
Requires:       fuse
Summary:        Userspace Trash Folder Enabling File System
Version:        1.0.0
Release:        1
License:        GPLv3+
Group:          System/Filesystems
Source:         %{name}-%{version}.tar.gz
Url:            http://code.google.com/p/collectfs/
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
 
%description
Collectfs is a FUSE userspace filesystem that provides add-on trash
collection for a directory hierarchy.  Any file that is overwritten
by remove (unlink), move, link, symlink, or open-truncate is relocated
to a trash directory (mount-point/.trash/).  Removed files are 
date-time stamped so that edit  history  is maintained (a version
number is appended if the same file is collected more than once in
the same second).

Collectfs is implemented as a userspace filesystem in an unprivileged
application using fuse (FUSE (Filesystem in USErspace)).
 
Authors:
--------
    Michael Hamilton <michael@actrix.gen.nz>
 
%prep
%setup
 
%build
export SUSE_ASNEEDED=0
CFLAGS="$RPM_OPT_FLAGS" \
CXXFLAGS="$RPM_OPT_FLAGS" \
make
 
%install
make PREFIX="$RPM_BUILD_ROOT/usr" install

%clean
test "$RPM_BUILD_ROOT" != "/" -a -d "$RPM_BUILD_ROOT" && rm -rf $RPM_BUILD_ROOT
 
%post
%{run_ldconfig}
 
%postun
%{run_ldconfig}

%defattr(-,root,root)
%doc COPYING* ChangeLog README*
%doc %{_mandir}/man?/collectfs*
%{_bindir}/collectfs*

%files
/usr/bin/collectfs
/usr/share/man/man1/collectfs.1.gz
 
%changelog
