#
# Main ffmpeg Makefile
# (c) 2000, 2001, 2002 Fabrice Bellard
#
include config.mak

VPATH=$(SRC_PATH)

CFLAGS= $(OPTFLAGS) -Wall -g -I. -I$(SRC_PATH) -I$(SRC_PATH)/libavcodec -I$(SRC_PATH)/libavformat -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE
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

ifeq ($(CONFIG_AUDIO_BEOS),yes)
EXTRALIBS+=-lmedia -lbe
endif

ifeq ($(BUILD_SHARED),yes)
DEP_LIBS=libavcodec/libavcodec.so libavformat/libavformat.a
else
DEP_LIBS=libavcodec/libavcodec.a libavformat/libavformat.a
ifeq ($(CONFIG_MP3LAME),yes)
EXTRALIBS+=-lmp3lame
endif
ifeq ($(CONFIG_VORBIS),yes)
EXTRALIBS+=-logg -lvorbis -lvorbisenc
endif
endif

ifeq ($(BUILD_VHOOK),yes)
VHOOK=videohook
INSTALLVHOOK=install-vhook
CLEANVHOOK=clean-vhook
ifneq ($(CONFIG_DARWIN),yes)
LDFLAGS += -rdynamic
endif
endif

OBJS = ffmpeg.o ffserver.o
SRCS = $(OBJS:.o=.c) $(ASM_OBJS:.o=.s)

all: lib $(PROG) $(VHOOK)

lib:
	$(MAKE) -C libavcodec all
	$(MAKE) -C libavformat all

ffmpeg_g$(EXE): ffmpeg.o $(DEP_LIBS)
	$(CC) $(LDFLAGS) -o $@ ffmpeg.o -L./libavcodec -L./libavformat \
              -lavformat -lavcodec $(EXTRALIBS)

ffmpeg$(EXE): ffmpeg_g$(EXE)
	cp -p $< $@ ; $(STRIP) $@

ffserver$(EXE): ffserver.o $(DEP_LIBS)
	$(CC) $(LDFLAGS) $(FFSLDFLAGS) \
		-o $@ ffserver.o -L./libavcodec -L./libavformat \
              -lavformat -lavcodec -ldl $(EXTRALIBS) 

ffplay: ffmpeg$(EXE)
	ln -sf $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 

videohook:
	$(MAKE) -C vhook all

install: all $(INSTALLVHOOK)
	$(MAKE) -C libavcodec install
	install -d $(prefix)/bin
	install -s -m 755 $(PROG) $(prefix)/bin
	ln -sf ffmpeg $(prefix)/bin/ffplay 

install-vhook: $(prefix)/lib/vhook
	$(MAKE) -C vhook install INSTDIR=$(prefix)/lib/vhook

$(prefix)/lib/vhook:
	mkdir $@

installlib:
	$(MAKE) -C libavcodec installlib
	$(MAKE) -C libavformat installlib

dep:	depend

depend:
	$(CC) -MM $(CFLAGS) $(SRCS) 1>.depend

clean: $(CLEANVHOOK)
	$(MAKE) -C libavcodec clean
	$(MAKE) -C libavformat clean
	$(MAKE) -C tests clean
	rm -f *.o *~ .depend gmon.out TAGS ffmpeg_g$(EXE) $(PROG) 

clean-vhook:
	$(MAKE) -C vhook clean

distclean: clean
	$(MAKE) -C libavcodec distclean
	rm -f config.mak config.h

TAGS:
	etags *.[ch] libavformat/*.[ch] libavcodec/*.[ch]

# regression tests

libavtest test mpeg4 mpeg: ffmpeg$(EXE)
	make -C tests $@

ifneq ($(wildcard .depend),)
include .depend
endif
