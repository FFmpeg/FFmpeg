#
# Main ffmpeg Makefile
# (c) 2000-2004 Fabrice Bellard
#
include config.mak

VPATH=$(SRC_PATH_BARE)

CFLAGS=$(OPTFLAGS) -I$(BUILD_ROOT) -I$(SRC_PATH) -I$(SRC_PATH)/libavutil \
       -I$(SRC_PATH)/libavcodec -I$(SRC_PATH)/libavformat -I$(SRC_PATH)/libswscale \
       -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_ISOC9X_SOURCE
LDFLAGS+= -g

ifeq ($(CONFIG_FFMPEG),yes)
MANPAGES=doc/ffmpeg.1
PROGS_G+=ffmpeg_g$(EXESUF)
PROGS+=ffmpeg$(EXESUF)
PROGTEST=output_example$(EXESUF)
QTFASTSTART=qt-faststart$(EXESUF)
endif

ifeq ($(CONFIG_FFSERVER),yes)
MANPAGES+=doc/ffserver.1
PROGS+=ffserver$(EXESUF)
endif

ifeq ($(CONFIG_FFPLAY),yes)
MANPAGES+=doc/ffplay.1
PROGS_G+=ffplay_g$(EXESUF)
PROGS+=ffplay$(EXESUF)
endif

BASENAMES=ffmpeg ffplay ffserver
ALLPROGS=$(addsuffix $(EXESUF), $(BASENAMES))
ALLPROGS_G=$(addsuffix _g$(EXESUF), $(BASENAMES))
ALLMANPAGES=$(addsuffix .1, $(BASENAMES))

ifeq ($(BUILD_SHARED),yes)
DEP_LIBS=libavcodec/$(SLIBPREF)avcodec$(SLIBSUF) libavformat/$(SLIBPREF)avformat$(SLIBSUF)
else
DEP_LIBS=libavcodec/$(LIBPREF)avcodec$(LIBSUF) libavformat/$(LIBPREF)avformat$(LIBSUF)
endif

ifeq ($(CONFIG_VHOOK),yes)
VHOOK=videohook
INSTALLVHOOK=install-vhook
endif

ifeq ($(BUILD_DOC),yes)
DOC=documentation
INSTALLMAN=install-man
endif

OBJS = ffmpeg.o ffserver.o cmdutils.o ffplay.o
SRCS = $(OBJS:.o=.c) $(ASM_OBJS:.o=.s)
LDFLAGS := -L$(BUILD_ROOT)/libavformat -L$(BUILD_ROOT)/libavcodec -L$(BUILD_ROOT)/libavutil $(LDFLAGS)
EXTRALIBS := -lavformat$(BUILDSUF) -lavcodec$(BUILDSUF) -lavutil$(BUILDSUF) $(EXTRALIBS)

ifeq ($(CONFIG_SWSCALER),yes)
LDFLAGS+=-L./libswscale
EXTRALIBS+=-lswscale$(BUILDSUF)
endif

all: lib $(PROGS_G) $(PROGS) $(PROGTEST) $(VHOOK) $(QTFASTSTART) $(DOC)

lib:
	$(MAKE) -C libavutil   all
	$(MAKE) -C libavcodec  all
	$(MAKE) -C libavformat all
ifeq ($(CONFIG_PP),yes)
	$(MAKE) -C libpostproc all
endif
ifeq ($(CONFIG_SWSCALER),yes)
	$(MAKE) -C libswscale  all
endif

ffmpeg_g$(EXESUF): ffmpeg.o cmdutils.o .libs
	$(CC) $(LDFLAGS) -o $@ ffmpeg.o cmdutils.o $(EXTRALIBS)

ffserver$(EXESUF): ffserver.o .libs
	$(CC) $(LDFLAGS) $(FFSERVERLDFLAGS) -o $@ ffserver.o $(EXTRALIBS)

ffplay_g$(EXESUF): ffplay.o cmdutils.o .libs
	$(CC) $(LDFLAGS) -o $@ ffplay.o cmdutils.o $(EXTRALIBS) $(SDL_LIBS)

%$(EXESUF): %_g$(EXESUF)
	cp -p $< $@
	$(STRIP) $@

version.h:
	$(SRC_PATH)/version.sh $(SRC_PATH)

output_example$(EXESUF): output_example.o .libs
	$(CC) $(LDFLAGS) -o $@ output_example.o $(EXTRALIBS)

qt-faststart$(EXESUF): qt-faststart.c
	$(CC) $(CFLAGS) $< -o $@

cws2fws$(EXESUF): cws2fws.c
	$(CC) $< -o $@ -lz

ffplay.o: ffplay.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c -o $@ $<

ffmpeg.o ffplay.o ffserver.o: version.h

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

videohook: .libs
	$(MAKE) -C vhook all

documentation:
	$(MAKE) -C doc all

install: install-progs install-libs install-headers $(INSTALLMAN) $(INSTALLVHOOK)

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

install-man:
	install -d "$(mandir)/man1"
	install -m 644 $(MANPAGES) "$(mandir)/man1"

install-vhook:
	$(MAKE) -C vhook install

install-libs:
	$(MAKE) -C libavutil   install-libs
	$(MAKE) -C libavcodec  install-libs
	$(MAKE) -C libavformat install-libs
ifeq ($(CONFIG_PP),yes)
	$(MAKE) -C libpostproc install-libs
endif
ifeq ($(CONFIG_SWSCALER),yes)
	$(MAKE) -C libswscale  install-libs
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
	$(MAKE) -C libswscale  install-headers

uninstall: uninstall-progs uninstall-libs uninstall-headers uninstall-man uninstall-vhook

uninstall-progs:
	rm -f $(addprefix $(bindir)/, $(ALLPROGS))

uninstall-man:
ifneq ($(CONFIG_MINGW),yes)
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

depend dep: .depend
	$(MAKE) -C libavutil   depend
	$(MAKE) -C libavcodec  depend
	$(MAKE) -C libavformat depend
ifeq ($(CONFIG_PP),yes)
	$(MAKE) -C libpostproc depend
endif
ifeq ($(CONFIG_SWSCALER),yes)
	$(MAKE) -C libswscale  depend
endif
ifeq ($(CONFIG_VHOOK),yes)
	$(MAKE) -C vhook       depend
endif

.depend: $(SRCS) version.h
	$(CC) -MM $(CFLAGS) $(SDL_CFLAGS) $(filter-out %.h,$^) 1>.depend

.libs: lib
	@test -f .libs || touch .libs
	@for i in $(DEP_LIBS) ; do if test $$i -nt .libs ; then touch .libs; fi ; done

clean:
	$(MAKE) -C libavutil   clean
	$(MAKE) -C libavcodec  clean
	$(MAKE) -C libavformat clean
	$(MAKE) -C libpostproc clean
	$(MAKE) -C libswscale  clean
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
	$(MAKE) -C libswscale  distclean
	$(MAKE) -C tests       distclean
	$(MAKE) -C vhook       distclean
	rm -f .depend version.h config.* *.pc

TAGS:
	etags *.[ch] libavformat/*.[ch] libavcodec/*.[ch]

# regression tests

codectest libavtest test-server fulltest test mpeg4 mpeg: $(PROGS)
	$(MAKE) -C tests $@

.PHONY: all lib videohook documentation install* wininstaller uninstall*
.PHONY: dep depend clean distclean TAGS
.PHONY: codectest libavtest test-server fulltest test mpeg4 mpeg

ifneq ($(wildcard .depend),)
include .depend
endif
