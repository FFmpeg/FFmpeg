#
# Main ffmpeg Makefile
# (c) 2000-2004 Fabrice Bellard
#
include config.mak

VPATH=$(SRC_PATH)

CFLAGS=$(OPTFLAGS) -I. -I$(SRC_PATH) -I$(SRC_PATH)/libavcodec -I$(SRC_PATH)/libavformat -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE
LDFLAGS+= -g 

ifeq ($(TARGET_GPROF),yes)
CFLAGS+=-p
LDFLAGS+=-p
endif

MANPAGE=$(SRC_PATH)/doc/ffmpeg.1
PROG=ffmpeg$(EXESUF)
PROGTEST=output_example$(EXESUF)
QTFASTSTART=qt-faststart$(EXESUF)

ifeq ($(CONFIG_FFSERVER),yes)
MANPAGE+=$(SRC_PATH)/doc/ffserver.1
PROG+=ffserver$(EXESUF)
endif

ifeq ($(CONFIG_FFPLAY),yes)
MANPAGE+=$(SRC_PATH)/doc/ffplay.1
PROG+=ffplay$(EXESUF)
endif

ifeq ($(CONFIG_AUDIO_BEOS),yes)
EXTRALIBS+=-lmedia -lbe
endif

ifeq ($(BUILD_SHARED),yes)
DEP_LIBS=libavcodec/$(SLIBPREF)avcodec$(SLIBSUF) libavformat/$(SLIBPREF)avformat$(SLIBSUF)
else
DEP_LIBS=libavcodec/$(LIBPREF)avcodec$(LIBSUF) libavformat/$(LIBPREF)avformat$(LIBSUF)
ifeq ($(CONFIG_MP3LAME),yes)
EXTRALIBS+=-lmp3lame
endif
endif

ifeq ($(CONFIG_VORBIS),yes)
EXTRALIBS+=-logg -lvorbis -lvorbisenc
endif

ifeq ($(CONFIG_FAAD),yes)
ifeq ($(CONFIG_FAADBIN),yes)
# no libs needed
else
EXTRALIBS += -lfaad
endif
endif

ifeq ($(CONFIG_FAAC),yes)
EXTRALIBS+=-lfaac
endif

ifeq ($(CONFIG_XVID),yes)
EXTRALIBS+=-lxvidcore
endif

ifeq ($(BUILD_VHOOK),yes)
VHOOK=videohook
INSTALLVHOOK=install-vhook
CLEANVHOOK=clean-vhook
endif

ifeq ($(TARGET_OS), SunOS)
TEST=/usr/bin/test
else
TEST=test
endif

ifeq ($(BUILD_DOC),yes)
DOC=documentation
endif

OBJS = ffmpeg.o ffserver.o cmdutils.o ffplay.o
SRCS = $(OBJS:.o=.c) $(ASM_OBJS:.o=.s)
FFLIBS = -L./libavformat -lavformat -L./libavcodec -lavcodec

all: lib $(PROG) $(PROGTEST) $(VHOOK) $(QTFASTSTART) $(DOC)

lib:
	$(MAKE) -C libavcodec all
	$(MAKE) -C libavformat all

ffmpeg_g$(EXESUF): ffmpeg.o cmdutils.o .libs
	$(CC) $(LDFLAGS) -o $@ ffmpeg.o cmdutils.o $(FFLIBS) $(EXTRALIBS)

ffmpeg$(EXESUF): ffmpeg_g$(EXESUF)
	cp -p $< $@
	$(STRIP) $@

ffserver$(EXESUF): ffserver.o .libs
	$(CC) $(LDFLAGS) $(FFSLDFLAGS) -o $@ ffserver.o $(FFLIBS) $(EXTRALIBS) 

ffplay_g$(EXESUF): ffplay.o cmdutils.o .libs
	$(CC) $(LDFLAGS) -o $@ ffplay.o cmdutils.o $(FFLIBS) $(EXTRALIBS) $(SDL_LIBS)

ffplay$(EXESUF): ffplay_g$(EXESUF)
	cp -p $< $@
	$(STRIP) $@

output_example$(EXESUF): output_example.o .libs
	$(CC) $(LDFLAGS) -o $@ output_example.o $(FFLIBS) $(EXTRALIBS)

qt-faststart$(EXESUF): qt-faststart.c
	$(CC) qt-faststart.c -o qt-faststart$(EXESUF)

ffplay.o: ffplay.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $< 

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 

videohook: .libs
	$(MAKE) -C vhook all

documentation:
	$(MAKE) -C doc all

.PHONY: install

install: all install-man $(INSTALLVHOOK)
	$(MAKE) -C libavcodec install
	$(MAKE) -C libavformat install
	install -d "$(bindir)"
	install -c -s -m 755 $(PROG) "$(bindir)"

# create the window installer
wininstaller: all install
	makensis ffinstall.nsi

# install man from source dir if available
install-man:
ifneq ($(CONFIG_WIN32),yes)
	if [ -f $(SRC_PATH)/doc/ffmpeg.1 ] ; then \
	    install -d "$(mandir)/man1" ; \
	    install -m 644 $(MANPAGE) "$(mandir)/man1" ; \
	fi
endif

install-vhook:
	$(MAKE) -C vhook install

installlib:
	$(MAKE) -C libavcodec installlib
	$(MAKE) -C libavformat installlib

dep:	depend

depend: .depend
	make -C libavcodec depend
	make -C libavformat depend
ifeq ($(BUILD_VHOOK),yes)
	make -C vhook depend
endif

.depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $(SDL_CFLAGS) $^ 1>.depend

.libs: lib
	@test -f .libs || touch .libs
	@for i in $(DEP_LIBS) ; do if $(TEST) $$i -nt .libs ; then touch .libs; fi ; done

clean: $(CLEANVHOOK)
	$(MAKE) -C libavcodec clean
	$(MAKE) -C libavformat clean
	$(MAKE) -C tests clean
	rm -f *.o *.d *~ .libs .depend gmon.out TAGS ffmpeg_g$(EXESUF) ffplay_g$(EXESUF) $(PROG) $(PROGTEST)

clean-vhook:
	$(MAKE) -C vhook clean

distclean: clean
	$(MAKE) -C libavcodec distclean
	rm -f config.mak config.h

TAGS:
	etags *.[ch] libavformat/*.[ch] libavcodec/*.[ch]

# regression tests

libavtest test mpeg4 mpeg test-server fulltest: ffmpeg$(EXESUF)
	$(MAKE) -C tests $@

# tar release (use 'make -k tar' on a checkouted tree)
FILE=ffmpeg-$(shell grep "\#define FFMPEG_VERSION " libavcodec/avcodec.h | \
                    cut -d "\"" -f 2 )

tar:
	rm -rf /tmp/$(FILE)
	cp -r . /tmp/$(FILE)
	( cd /tmp ; tar zcvf ~/$(FILE).tar.gz $(FILE) --exclude CVS )
	rm -rf /tmp/$(FILE)

.PHONY: lib

ifneq ($(wildcard .depend),)
include .depend
endif
