# Main ffmpeg Makefile
# (c) 2000, 2001, 2002 Gerard Lantau
#
include config.mak

VPATH=$(SRC_PATH)

CFLAGS= $(OPTFLAGS) -Wall -g -I. -I$(SRC_PATH) -I$(SRC_PATH)/libavcodec -I$(SRC_PATH)/libav
LDFLAGS= -g
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
FFMPEG_LIB=-Llibavcodec -lffmpeg
DEP_FFMPEG_LIB=
else
FFMPEG_LIB=libavcodec/libavcodec.a
DEP_FFMPEG_LIB=libavcodec/libavcodec.a
ifeq ($(CONFIG_MP3LAME),yes)
EXTRALIBS+=-lmp3lame
endif
endif

OBJS = ffmpeg.o ffserver.o
SRCS = $(OBJS:.o=.c) $(ASM_OBJS:.o=.s)

all: lib $(PROG)

lib:
	$(MAKE) -C libavcodec all
	$(MAKE) -C libav all

ffmpeg$(EXE): ffmpeg.o libav/libav.a $(DEP_FFMPEG_LIB)
	$(CC) $(LDFLAGS) -o $@ $^ $(FFMPEG_LIB) $(EXTRALIBS)

ffserver$(EXE): ffserver.o libav/libav.a $(DEP_FFMPEG_LIB)
	$(CC) $(LDFLAGS) -o $@ $^ $(FFMPEG_LIB) $(EXTRALIBS)

ffplay: ffmpeg$(EXE)
	ln -sf $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 

install: all
	$(MAKE) -C libavcodec install
	install -s -m 755 $(PROG) $(prefix)/bin
	ln -sf ffmpeg $(prefix)/bin/ffplay 

dep:	depend

depend:
	$(CC) -MM $(CFLAGS) $(SRCS) 1>.depend

clean: 
	$(MAKE) -C libavcodec clean
	$(MAKE) -C libav clean
	$(MAKE) -C tests clean
	rm -f *.o *~ .depend gmon.out TAGS $(PROG) 

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
