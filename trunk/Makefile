#  Copyright 2011, Michael Hamilton
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

doc : collectfs.1.html collectfs.1.man
	echo done doc

clean :
	rm -f collectfs *.o

dist :
	rm -rf dist/collectfs/
	mkdir -p dist/collectfs/
	cp Makefile *.c *.h *.1 COPYING README dist/collectfs/
	cd dist/; tar cvzf ../collectfs.tar.gz collectfs/
