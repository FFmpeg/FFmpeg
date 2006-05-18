#
# Main ffmpeg Makefile
# (c) 2000-2004 Fabrice Bellard
#
include config.mak

VPATH=$(SRC_PATH)

CFLAGS=$(OPTFLAGS) -I. -I$(SRC_PATH) -I$(SRC_PATH)/libavutil -I$(SRC_PATH)/libavcodec -I$(SRC_PATH)/libavformat -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE
LDFLAGS+= -g

ifeq ($(TARGET_GPROF),yes)
CFLAGS+=-p
LDFLAGS+=-p
endif

MANPAGE=doc/ffmpeg.1
PROG_G+=ffmpeg_g$(EXESUF)
PROG+=ffmpeg$(EXESUF)
PROGTEST=output_example$(EXESUF)
QTFASTSTART=qt-faststart$(EXESUF)

ifeq ($(CONFIG_FFSERVER),yes)
MANPAGE+=doc/ffserver.1
PROG+=ffserver$(EXESUF)
endif

ifeq ($(CONFIG_FFPLAY),yes)
MANPAGE+=doc/ffplay.1
PROG_G+=ffplay_g$(EXESUF)
PROG+=ffplay$(EXESUF)
FFPLAY_O=ffplay.o
endif

ifeq ($(CONFIG_AUDIO_BEOS),yes)
EXTRALIBS+=-lmedia -lbe
endif

ifeq ($(BUILD_SHARED),yes)
DEP_LIBS=libavcodec/$(SLIBPREF)avcodec$(SLIBSUF) libavformat/$(SLIBPREF)avformat$(SLIBSUF)
else
DEP_LIBS=libavcodec/$(LIBPREF)avcodec$(LIBSUF) libavformat/$(LIBPREF)avformat$(LIBSUF)
endif

ifeq ($(BUILD_VHOOK),yes)
VHOOK=videohook
INSTALLVHOOK=install-vhook
endif

ifeq ($(TARGET_OS), SunOS)
TEST=/usr/bin/test
else
TEST=test
endif

ifeq ($(BUILD_DOC),yes)
DOC=documentation
endif

OBJS = ffmpeg.o ffserver.o cmdutils.o $(FFPLAY_O)
SRCS = $(OBJS:.o=.c) $(ASM_OBJS:.o=.s)
FFLIBS = -L./libavformat -lavformat$(BUILDSUF) -L./libavcodec -lavcodec$(BUILDSUF) -L./libavutil -lavutil$(BUILDSUF)

all: lib $(PROG_G) $(PROG) $(PROGTEST) $(VHOOK) $(QTFASTSTART) $(DOC)

lib:
	$(MAKE) -C libavutil   all
	$(MAKE) -C libavcodec  all
	$(MAKE) -C libavformat all
ifeq ($(CONFIG_PP),yes)
	$(MAKE) -C libavcodec/libpostproc all
endif

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
	$(CC) $(CFLAGS) $(SRC_PATH)/qt-faststart.c -o qt-faststart$(EXESUF)

cws2fws$(EXESUF): cws2fws.c
	$(CC) $(SRC_PATH)/cws2fws.c -o cws2fws$(EXESUF) -lz

ffplay.o: ffplay.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

videohook: .libs
	$(MAKE) -C vhook all

documentation:
	$(MAKE) -C doc all

.PHONY: install

install: install-progs install-libs install-headers install-man $(INSTALLVHOOK)

ifeq ($(BUILD_SHARED),yes)
install-progs: $(PROG) install-libs
else
install-progs: $(PROG)
endif
	install -d "$(bindir)"
	install -c $(INSTALLSTRIP) -m 755 $(PROG) "$(bindir)"

# create the window installer
wininstaller: all install
	makensis ffinstall.nsi

# install man from source dir if available
install-man:
ifneq ($(CONFIG_WIN32),yes)
	if [ -f doc/ffmpeg.1 ] ; then \
	    install -d "$(mandir)/man1" ; \
	    install -m 644 $(MANPAGE) "$(mandir)/man1" ; \
	fi
endif

install-vhook:
	$(MAKE) -C vhook install

install-libs:
	$(MAKE) -C libavutil   install-libs
	$(MAKE) -C libavcodec  install-libs
	$(MAKE) -C libavformat install-libs
ifeq ($(CONFIG_PP),yes)
	$(MAKE) -C libavcodec/libpostproc install-libs
endif
ifeq ($(BUILD_SHARED),yes)
	-$(LDCONFIG)
endif

install-headers:
	$(MAKE) -C libavutil   install-headers
	$(MAKE) -C libavcodec  install-headers
	$(MAKE) -C libavformat install-headers
ifeq ($(CONFIG_PP),yes)
	$(MAKE) -C libavcodec/libpostproc install-headers
endif

dep:	depend

depend: .depend
	$(MAKE) -C libavcodec  depend
	$(MAKE) -C libavformat depend
ifeq ($(BUILD_VHOOK),yes)
	$(MAKE) -C vhook       depend
endif

.depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $(SDL_CFLAGS) $^ 1>.depend

.libs: lib
	@test -f .libs || touch .libs
	@for i in $(DEP_LIBS) ; do if $(TEST) $$i -nt .libs ; then touch .libs; fi ; done

clean:
	$(MAKE) -C libavutil   clean
	$(MAKE) -C libavcodec  clean
	$(MAKE) -C libavformat clean
	$(MAKE) -C libavcodec/libpostproc clean
	$(MAKE) -C tests       clean
	$(MAKE) -C vhook       clean
	rm -f *.o *.d *~ .libs gmon.out TAGS \
	   $(PROG) $(PROG_G) $(PROGTEST) $(QTFASTSTART)

# Note well: config.log is NOT removed.
distclean: clean
	$(MAKE) -C libavutil   distclean
	$(MAKE) -C libavcodec  distclean
	$(MAKE) -C libavformat distclean
	$(MAKE) -C libavcodec/libpostproc distclean
	$(MAKE) -C tests       distclean
	$(MAKE) -C vhook       distclean
	rm -f .depend config.mak config.h *.pc

TAGS:
	etags *.[ch] libavformat/*.[ch] libavcodec/*.[ch]

# regression tests

libavtest test mpeg4 mpeg test-server fulltest: $(PROG)
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
