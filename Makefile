# Main ffmpeg Makefile
# (c) 2000, 2001 Gerard Lantau
#
include config.mk

CFLAGS= -O2 -Wall -g -I./libavcodec -I./libav 
LDFLAGS= -g 
ifdef CONFIG_GPROF
CFLAGS+=-p
LDFLAGS+=-p
endif

PROG= ffmpeg ffserver

all: lib $(PROG)

lib:
	make -C libavcodec all
	make -C libav all

ffmpeg: ffmpeg.o libav/libav.a libavcodec/libavcodec.a
	gcc $(LDFLAGS) -o $@ $^ -lm

ffserver: ffserver.o libav/libav.a libavcodec/libavcodec.a
	gcc $(LDFLAGS) -o $@ $^ -lm

%.o: %.c
	gcc $(CFLAGS) -c -o $@ $< 

install: all
	install -s -m 755 $(PROG) $(PREFIX)/bin

clean: 
	make -C libavcodec clean
	make -C libav clean
	rm -f *.o *~ gmon.out TAGS $(PROG) 

distclean: clean
	rm -f Rules.mk config.h

TAGS:
	etags *.[ch] libav/*.[ch] libavcodec/*.[ch]
