#
# Main ffmpeg Makefile
# (c) 2000, 2001, 2002 Fabrice Bellard
#
include config.mak

VPATH=$(SRC_PATH)

CFLAGS= $(OPTFLAGS) -Wall -g -I. -I$(SRC_PATH) -I$(SRC_PATH)/libavcodec -I$(SRC_PATH)/libav -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE
ifeq ($(CONFIG_DARWIN),yes)
LDFLAGS+= -g -d
FFSLDFLAGS= -Wl,-bind_at_load
else
LDFLAGS+= -g -Wl,--warn-common
FFSLDFLAGS= -Wl,-E
endif

ifeq ($(TARGET_GPROF),yes)
CFLAGS+=-p
LDFLAGS+=-p
endif

ifeq ($(CONFIG_WIN32),yes)
EXE=.exe
PROG=ffmpeg$(EXE)
else
EXT=
PROG=ffmpeg ffplay ffserver
endif


ifeq ($(BUILD_SHARED),yes)
DEP_LIBS=libavcodec/libavcodec.so libav/libavformat.a
else
DEP_LIBS=libavcodec/libavcodec.a libav/libavformat.a
ifeq ($(CONFIG_MP3LAME),yes)
EXTRALIBS+=-lmp3lame
endif
ifeq ($(CONFIG_VORBIS),yes)
EXTRALIBS+=-logg -lvorbis -lvorbisenc
endif
endif

OBJS = ffmpeg.o ffserver.o
SRCS = $(OBJS:.o=.c) $(ASM_OBJS:.o=.s)

all: lib $(PROG)

lib:
	$(MAKE) -C libavcodec all
	$(MAKE) -C libav all

ffmpeg_g$(EXE): ffmpeg.o $(DEP_LIBS)
	$(CC) $(LDFLAGS) -o $@ ffmpeg.o -L./libavcodec -L./libav \
              -lavformat -lavcodec $(EXTRALIBS)

ffmpeg$(EXE): ffmpeg_g$(EXE)
	$(STRIP) -o $@ $< ; chmod --reference=$< $@

ffserver$(EXE): ffserver.o $(DEP_LIBS)
	$(CC) $(LDFLAGS) $(FFSLDFLAGS) \
		-o $@ ffserver.o -L./libavcodec -L./libav \
              -lavformat -lavcodec -ldl $(EXTRALIBS) 

ffplay: ffmpeg$(EXE)
	ln -sf $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 

install: all
	$(MAKE) -C libavcodec install
	install -d $(prefix)/bin
	install -s -m 755 $(PROG) $(prefix)/bin
	ln -sf ffmpeg $(prefix)/bin/ffplay 

installlib:
	$(MAKE) -C libavcodec installlib
	$(MAKE) -C libav installlib

dep:	depend

depend:
	$(CC) -MM $(CFLAGS) $(SRCS) 1>.depend

clean: 
	$(MAKE) -C libavcodec clean
	$(MAKE) -C libav clean
	$(MAKE) -C tests clean
	rm -f *.o *~ .depend gmon.out TAGS ffmpeg_g$(EXE) $(PROG) 

distclean: clean
	$(MAKE) -C libavcodec distclean
	rm -f config.mak config.h

TAGS:
	etags *.[ch] libav/*.[ch] libavcodec/*.[ch]

# regression tests

libavtest test mpeg4 mpeg: ffmpeg$(EXE)
	make -C tests $@

ifneq ($(wildcard .depend),)
include .depend
endif
