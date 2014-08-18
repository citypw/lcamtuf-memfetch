#
#  memfetch 0.05b - Linux on-demand process image dump
#  ---------------------------------------------------
#  Copyright (C) 2002, 2003 by Michal Zalewski <lcamtuf@coredump.cx>
#
#  Licensed under terms and conditions of GNU Public License version 2.
#

FILE   = memfetch
CFLAGS = -Wall -O9
CC     = gcc

all: $(FILE)

fc:
	rm -f map-*.bin mem-*.bin mfetch.lst

clean: fc
	rm -f $(FILE) core.[0123456789]* core *.o a.out dupa

publish: clean
	cd /; tar cfvz memfetch.tgz /memfetch
	scp -p /memfetch.tgz lcamtuf@coredump.cx:/export/www/lcamtuf/soft/
	rm -f /memfetch.tgz

install: all
	cp -f memfetch /usr/local/bin/memfetch
	cp -f mffind.pl /usr/local/bin/mffind.pl

