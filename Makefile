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
endif

all: lib $(PROG)

lib:
	$(MAKE) -C libavcodec all
	$(MAKE) -C libav all

ffmpeg$(EXE): ffmpeg.o libav/libav.a $(DEP_FFMPEG_LIB)
	$(CC) $(LDFLAGS) -lm -o $@ $^ $(FFMPEG_LIB)

ffserver$(EXE): ffserver.o libav/libav.a $(DEP_FFMPEG_LIB)
	$(CC) $(LDFLAGS) -lm -o $@ $^ $(FFMPEG_LIB)

ffplay: ffmpeg$(EXE)
	ln -sf $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 

install: all
	$(MAKE) -C libavcodec install
	install -s -m 755 $(PROG) $(prefix)/bin
	ln -sf ffmpeg $(prefix)/bin/ffplay 

clean: 
	$(MAKE) -C libavcodec clean
	$(MAKE) -C libav clean
	rm -f *.o *~ gmon.out TAGS $(PROG) 

distclean: clean
	$(MAKE) -C libavcodec distclean
	rm -f config.mak config.h

TAGS:
	etags *.[ch] libav/*.[ch] libavcodec/*.[ch]
