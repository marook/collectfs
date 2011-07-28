# Makefile for collectfs
#  Copyright 2011, Michael Hamilton
#
#####################
# User configuration:
#####################
# Set this to the directory path of the executable
INSTALLDIR = /usr/bin
#
# Set this to the directory for manual pages
MANUALDIR  = /usr/share/man
#
#####################

PROGNAME = collectfs

ifeq ($(origin FUSE_C_FLAGS), undefined)
FUSE_C_FLAGS := $(shell pkg-config fuse --cflags)
endif

ifeq ($(origin FUSE_LD_FLAGS), undefined)
FUSE_LD_FLAGS := $(shell pkg-config fuse --libs)
endif

MANDIR  ?= $(MANUALDIR)
BINDIR  ?= $(INSTALLDIR)
LDFLAGS ?= $(FUSE_LD_FLAGS)
CFLAGS  ?= $(FUSE_C_FLAGS) 

.PHONY : all doc install clean dist

all : $(PROGNAME)

$(PROGNAME) : $(PROGNAME).o log.o
	gcc -g -o $(PROGNAME) $(PROGNAME).o log.o $(LDFLAGS)

$(PROGNAME).o : $(PROGNAME).c log.h
	gcc -O2 -g -Wall $(CFLAGS) $(OPTFLAGS) -c $(PROGNAME).c

log.o : log.c log.h
	gcc -O2 -g -Wall $(CFLAGS) $(OPTFLAGS) -c log.c

$(PROGNAME).1.html : $(PROGNAME).1
	groff -man -T html $(PROGNAME).1 > $(PROGNAME).1.html

$(PROGNAME).1.man : $(PROGNAME).1
	groff -man -T ascii $(PROGNAME).1 > $(PROGNAME).1.man

$(PROGNAME).1.gz : $(PROGNAME).1
	cat $(PROGNAME).1 | gzip -9 > $(PROGNAME).1.gz

doc : $(PROGNAME).1.html $(PROGNAME).1.man $(PROGNAME).1.gz

install : $(PROGNAME) doc
	install -d -m 755 $(DESTDIR)$(BINDIR)
	install -d -m 755 $(DESTDIR)$(MANDIR)/man1
	install -m 755 $(PROGNAME) $(DESTDIR)$(BINDIR)/
	install -m 644 $(PROGNAME).1.gz $(DESTDIR)$(MANDIR)/man1/

clean :
	rm -f $(PROGNAME) $(PROGNAME).1.gz *.o

dist :
	rm -rf distfiles/$(PROGNAME)/
	mkdir -p distfiles/$(PROGNAME)/
	cp Makefile *.c *.h *.1 *.html *.man COPYING README distfiles/$(PROGNAME)/
	cd distfiles/; tar cvzf ../$(PROGNAME).tar.gz $(PROGNAME)/
