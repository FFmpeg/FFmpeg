# Main ffmpeg Makefile
# (c) 2000, 2001 Gerard Lantau
#
include config.mak

CFLAGS= $(OPTFLAGS) -Wall -g -I./libavcodec -I./libav 
LDFLAGS= -g 
ifeq ($(TARGET_GPROF),yes)
CFLAGS+=-p
LDFLAGS+=-p
endif

PROG= ffmpeg ffserver

all: lib $(PROG)

lib:
	$(MAKE) -C libavcodec all
	$(MAKE) -C libav all

ffmpeg: ffmpeg.o libav/libav.a libavcodec/libavcodec.a
	gcc $(LDFLAGS) -o $@ $^ -lm

ffserver: ffserver.o libav/libav.a libavcodec/libavcodec.a
	gcc $(LDFLAGS) -o $@ $^ -lm

%.o: %.c
	gcc $(CFLAGS) -c -o $@ $< 

install: all
	install -s -m 755 $(PROG) $(prefix)/bin

clean: 
	$(MAKE) -C libavcodec clean
	$(MAKE) -C libav clean
	rm -f *.o *~ gmon.out TAGS $(PROG) 

distclean: clean
	$(MAKE) -C libavcodec distclean
	rm -f config.mak config.h

TAGS:
	etags *.[ch] libav/*.[ch] libavcodec/*.[ch]
