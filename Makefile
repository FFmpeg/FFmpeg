#
# Main ffmpeg Makefile
# (c) 2000-2004 Fabrice Bellard
#
include config.mak

VPATH=$(SRC_PATH)

CFLAGS=$(OPTFLAGS) -I. -I$(SRC_PATH) -I$(SRC_PATH)/libavutil \
       -I$(SRC_PATH)/libavcodec -I$(SRC_PATH)/libavformat \
       -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE
LDFLAGS+= -g

MANPAGES=doc/ffmpeg.1
PROGS_G+=ffmpeg_g$(EXESUF)
PROGS+=ffmpeg$(EXESUF)
PROGTEST=output_example$(EXESUF)
QTFASTSTART=qt-faststart$(EXESUF)

ifeq ($(CONFIG_FFSERVER),yes)
MANPAGES+=doc/ffserver.1
PROGS+=ffserver$(EXESUF)
endif

ifeq ($(CONFIG_FFPLAY),yes)
MANPAGES+=doc/ffplay.1
PROGS_G+=ffplay_g$(EXESUF)
PROGS+=ffplay$(EXESUF)
FFPLAY_O=ffplay.o
endif

BASENAMES=ffmpeg ffplay ffserver
ALLPROGS=$(addsuffix $(EXESUF), $(BASENAMES))
ALLPROGS_G=$(addsuffix _g$(EXESUF), $(BASENAMES))
ALLMANPAGES=$(addsuffix .1, $(BASENAMES))

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

ifeq ($(BUILD_DOC),yes)
DOC=documentation
endif

OBJS = ffmpeg.o ffserver.o cmdutils.o $(FFPLAY_O)
SRCS = $(OBJS:.o=.c) $(ASM_OBJS:.o=.s)
FFLIBDIRS = -L./libavformat -L./libavcodec -L./libavutil
FFLIBS = -lavformat$(BUILDSUF) -lavcodec$(BUILDSUF) -lavutil$(BUILDSUF)

all: version.h lib $(PROGS_G) $(PROGS) $(PROGTEST) $(VHOOK) $(QTFASTSTART) $(DOC)

lib:
	$(MAKE) -C libavutil   all
	$(MAKE) -C libavcodec  all
	$(MAKE) -C libavformat all
ifeq ($(CONFIG_PP),yes)
	$(MAKE) -C libpostproc all
endif

ffmpeg_g$(EXESUF): ffmpeg.o cmdutils.o .libs
	$(CC) $(FFLIBDIRS) $(LDFLAGS) -o $@ ffmpeg.o cmdutils.o $(FFLIBS) $(EXTRALIBS)

ffserver$(EXESUF): ffserver.o .libs
	$(CC) $(FFLIBDIRS) $(LDFLAGS) $(FFSLDFLAGS) -o $@ ffserver.o $(FFLIBS) $(EXTRALIBS)

ffplay_g$(EXESUF): ffplay.o cmdutils.o .libs
	$(CC) $(FFLIBDIRS) $(LDFLAGS) -o $@ ffplay.o cmdutils.o $(FFLIBS) $(EXTRALIBS) $(SDL_LIBS)

%$(EXESUF): %_g$(EXESUF)
	cp -p $< $@
	$(STRIP) $@

.PHONY: version.h
version.h:
	$(SRC_PATH)/version.sh "$(SRC_PATH)"

output_example$(EXESUF): output_example.o .libs
	$(CC) $(FFLIBDIRS) $(LDFLAGS) -o $@ output_example.o $(FFLIBS) $(EXTRALIBS)

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
install-progs: $(PROGS) install-libs
else
install-progs: $(PROGS)
endif
	install -d "$(bindir)"
	install -c $(INSTALLSTRIP) -m 755 $(PROGS) "$(bindir)"

# create the window installer
wininstaller: all install
	makensis ffinstall.nsi

# install man from source dir if available
install-man:
ifneq ($(CONFIG_WIN32),yes)
	if [ -f doc/ffmpeg.1 ] ; then \
	    install -d "$(mandir)/man1" ; \
	    install -m 644 $(MANPAGES) "$(mandir)/man1" ; \
	fi
endif

install-vhook:
	$(MAKE) -C vhook install

install-libs:
	$(MAKE) -C libavutil   install-libs
	$(MAKE) -C libavcodec  install-libs
	$(MAKE) -C libavformat install-libs
ifeq ($(CONFIG_PP),yes)
	$(MAKE) -C libpostproc install-libs
endif
ifeq ($(BUILD_SHARED),yes)
	-$(LDCONFIG)
endif

install-headers:
	$(MAKE) -C libavutil   install-headers
	$(MAKE) -C libavcodec  install-headers
	$(MAKE) -C libavformat install-headers
ifeq ($(CONFIG_PP),yes)
	$(MAKE) -C libpostproc install-headers
endif

uninstall: uninstall-progs uninstall-libs uninstall-headers uninstall-man uninstall-vhook

uninstall-progs:
	rm -f $(addprefix $(bindir)/, $(ALLPROGS))

uninstall-man:
ifneq ($(CONFIG_WIN32),yes)
	rm -f $(addprefix $(mandir)/man1/,$(ALLMANPAGES))
endif

uninstall-vhook:
	$(MAKE) -C vhook uninstall

uninstall-libs:
	$(MAKE) -C libavutil   uninstall-libs
	$(MAKE) -C libavcodec  uninstall-libs
	$(MAKE) -C libavformat uninstall-libs
	$(MAKE) -C libpostproc uninstall-libs

uninstall-headers:
	$(MAKE) -C libavutil   uninstall-headers
	$(MAKE) -C libavcodec  uninstall-headers
	$(MAKE) -C libavformat uninstall-headers
	$(MAKE) -C libpostproc uninstall-headers
	-rmdir "$(incdir)"
	-rmdir "$(prefix)/include/postproc"

dep:	depend

depend: .depend
	$(MAKE) -C libavutil   depend
	$(MAKE) -C libavcodec  depend
	$(MAKE) -C libavformat depend
ifeq ($(BUILD_VHOOK),yes)
	$(MAKE) -C vhook       depend
endif

.depend: $(SRCS) version.h
	$(CC) -MM $(CFLAGS) $(SDL_CFLAGS) $^ 1>.depend

.libs: lib
	@test -f .libs || touch .libs
	@for i in $(DEP_LIBS) ; do if test $$i -nt .libs ; then touch .libs; fi ; done

clean:
	$(MAKE) -C libavutil   clean
	$(MAKE) -C libavcodec  clean
	$(MAKE) -C libavformat clean
	$(MAKE) -C libpostproc clean
	$(MAKE) -C tests       clean
	$(MAKE) -C vhook       clean
	$(MAKE) -C doc         clean
	rm -f *.o *.d *~ .libs gmon.out TAGS \
	   $(ALLPROGS) $(ALLPROGS_G) $(PROGTEST) $(QTFASTSTART)

# Note well: config.log is NOT removed.
distclean: clean
	$(MAKE) -C libavutil   distclean
	$(MAKE) -C libavcodec  distclean
	$(MAKE) -C libavformat distclean
	$(MAKE) -C libpostproc distclean
	$(MAKE) -C tests       distclean
	$(MAKE) -C vhook       distclean
	rm -f .depend version.h config.* *.pc

TAGS:
	etags *.[ch] libavformat/*.[ch] libavcodec/*.[ch]

# regression tests

libavtest test mpeg4 mpeg test-server fulltest: $(PROGS)
	$(MAKE) -C tests $@

# tar release (use 'make -k tar' on a checkouted tree)
FILE=ffmpeg-$(shell grep "\#define FFMPEG_VERSION " libavcodec/avcodec.h | \
                    cut -d "\"" -f 2 )

tar:
	rm -rf /tmp/$(FILE)
	cp -r . /tmp/$(FILE)
	( cd /tmp ; tar zcvf ~/$(FILE).tar.gz $(FILE) --exclude .svn )
	rm -rf /tmp/$(FILE)

.PHONY: lib

ifneq ($(wildcard .depend),)
include .depend
endif
