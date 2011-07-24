#  Copyright 2011, Michael Hamilton

VERSION = 1.0.0
PREFIX = /usr
BINDIR = $(PREFIX)/bin/
MANDIR = $(PREFIX)/share/man/man1/
RPM_SRCDIR = /usr/src/packages/SOURCES/
RPM_SPECDIR = /usr/src/packages/SPECS/

collectfs : collectfs.o log.o
	gcc -g `pkg-config fuse --libs` -o collectfs collectfs.o log.o

collectfs.o : collectfs.c log.h
	gcc -O2 -g -Wall `pkg-config fuse --cflags` -c collectfs.c

log.o : log.c log.h
	gcc -O2 -g -Wall `pkg-config fuse --cflags` -c log.c

collectfs.1.html : collectfs.1
	groff -man -T html collectfs.1 > collectfs.1.html

collectfs.1.man : collectfs.1
	groff -man -T ascii collectfs.1 > collectfs.1.man

install : collectfs
	install -D collectfs $(BINDIR)/collectfs
	install -D collectfs.1 $(MANDIR)/collectfs.1

doc : collectfs.1.html collectfs.1.man
	echo done doc

clean :
	rm -f collectfs *.o 
	rm -rf distfiles

dist :
	grep $(VERSION) collectfs.c collectfs.spec
	rm -rf distfiles/collectfs-$(VERSION)/
	mkdir -p distfiles/collectfs-$(VERSION)/
	cp Makefile *.c *.h *.1 *.html *.man *.spec COPYING README distfiles/collectfs-$(VERSION)/
	cd distfiles/; tar cvzf ../collectfs-$(VERSION).tar.gz collectfs-$(VERSION)/

rpm : dist
	cp collectfs.spec $(RPM_SPECDIR)
	cp collectfs-$(VERSION).tar.gz $(RPM_SRCDIR)
	rpmbuild -ba -v $(RPM_SPECDIR)/collectfs.spec
