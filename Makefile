#
# Main ffmpeg Makefile
# (c) 2000-2004 Fabrice Bellard
#
include config.mak

VPATH=$(SRC_PATH_BARE)

CFLAGS=$(OPTFLAGS) -I$(BUILD_ROOT) -I$(SRC_PATH) -I$(SRC_PATH)/libavutil \
       -I$(SRC_PATH)/libavcodec -I$(SRC_PATH)/libavformat -I$(SRC_PATH)/libswscale \
       -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_ISOC9X_SOURCE -DHAVE_AV_CONFIG_H
LDFLAGS+= -g

PROGS-$(CONFIG_FFMPEG)   += ffmpeg
PROGS-$(CONFIG_FFPLAY)   += ffplay
PROGS-$(CONFIG_FFSERVER) += ffserver

PROGS       = $(addsuffix   $(EXESUF), $(PROGS-yes))
PROGS_G     = $(addsuffix _g$(EXESUF), $(PROGS-yes))
MANPAGES    = $(addprefix doc/, $(addsuffix .1, $(PROGS-yes)))

BASENAMES   = ffmpeg ffplay ffserver
ALLPROGS    = $(addsuffix   $(EXESUF), $(BASENAMES))
ALLPROGS_G  = $(addsuffix _g$(EXESUF), $(BASENAMES))
ALLMANPAGES = $(addsuffix .1, $(BASENAMES))

ifeq ($(BUILD_SHARED),yes)
DEP_LIBS=libavcodec/$(SLIBPREF)avcodec$(SLIBSUF) libavformat/$(SLIBPREF)avformat$(SLIBSUF)
else
DEP_LIBS=libavcodec/$(LIBPREF)avcodec$(LIBSUF) libavformat/$(LIBPREF)avformat$(LIBSUF)
endif

ifeq ($(CONFIG_VHOOK),yes)
all: videohook
install: install-vhook
endif

ifeq ($(BUILD_DOC),yes)
all: documentation
install: install-man
endif

SRCS = $(addsuffix .c, $(PROGS-yes)) cmdutils.c
LDFLAGS := -L$(BUILD_ROOT)/libavformat -L$(BUILD_ROOT)/libavcodec -L$(BUILD_ROOT)/libavutil $(LDFLAGS)
EXTRALIBS := -lavformat$(BUILDSUF) -lavcodec$(BUILDSUF) -lavutil$(BUILDSUF) $(EXTRALIBS)

ifeq ($(CONFIG_SWSCALER),yes)
LDFLAGS+=-L$(BUILD_ROOT)/libswscale
EXTRALIBS+=-lswscale$(BUILDSUF)
endif

all: lib $(PROGS)

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

SVN_ENTRIES = $(SRC_PATH_BARE)/.svn/entries
ifeq ($(wildcard $(SVN_ENTRIES)),$(SVN_ENTRIES))
version.h: $(SVN_ENTRIES)
endif

version.h:
	$(SRC_PATH)/version.sh $(SRC_PATH)

output_example$(EXESUF): output_example.o .libs
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(EXTRALIBS)

qt-faststart$(EXESUF): qt-faststart.c
	$(CC) $(CFLAGS) $< -o $@

cws2fws$(EXESUF): cws2fws.c
	$(CC) $(CFLAGS) $< -o $@ -lz

ffplay.o: CFLAGS += $(SDL_CFLAGS)

ffmpeg.o ffplay.o ffserver.o: version.h

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

videohook: .libs
	$(MAKE) -C vhook all

documentation: $(addprefix doc/, ffmpeg-doc.html faq.html ffserver-doc.html \
                                 ffplay-doc.html hooks.html $(ALLMANPAGES))

doc/%.html: doc/%.texi
	texi2html -monolithic -number $<
	mv $(@F) $@

doc/%.pod: doc/%-doc.texi
	doc/texi2pod.pl $< $@

doc/%.1: doc/%.pod
	pod2man --section=1 --center=" " --release=" " $< > $@

install: install-progs install-libs install-headers

ifeq ($(BUILD_SHARED),yes)
install-progs: $(PROGS) install-libs
else
install-progs: $(PROGS)
endif
	install -d "$(bindir)"
	install -c -m 755 $(PROGS) "$(bindir)"

# Create the Windows installer.
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
	rm -f $(addprefix $(mandir)/man1/,$(ALLMANPAGES))

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

$(DEP_LIBS): lib

.libs: $(DEP_LIBS)
	touch $@

clean:
	$(MAKE) -C libavutil   clean
	$(MAKE) -C libavcodec  clean
	$(MAKE) -C libavformat clean
	$(MAKE) -C libpostproc clean
	$(MAKE) -C libswscale  clean
	$(MAKE) -C vhook       clean
	rm -f *.o *~ .libs gmon.out TAGS $(ALLPROGS) $(ALLPROGS_G) \
	   output_example$(EXESUF) qt-faststart$(EXESUF) cws2fws$(EXESUF)
	rm -f doc/*.html doc/*.pod doc/*.1
	rm -rf tests/vsynth1 tests/vsynth2 tests/data tests/asynth1.sw tests/*~
	rm -f $(addprefix tests/,$(addsuffix $(EXESUF),audiogen videogen rotozoom seek_test tiny_psnr))

distclean: clean
	$(MAKE) -C libavutil   distclean
	$(MAKE) -C libavcodec  distclean
	$(MAKE) -C libavformat distclean
	$(MAKE) -C libpostproc distclean
	$(MAKE) -C libswscale  distclean
	$(MAKE) -C vhook       distclean
	rm -f .depend version.h config.* *.pc

TAGS:
	etags *.[ch] libavformat/*.[ch] libavcodec/*.[ch]

# regression tests

fulltest test: codectest libavtest seektest

FFMPEG_REFFILE   = $(SRC_PATH)/tests/ffmpeg.regression.ref
FFSERVER_REFFILE = $(SRC_PATH)/tests/ffserver.regression.ref
LIBAV_REFFILE    = $(SRC_PATH)/tests/libav.regression.ref
ROTOZOOM_REFFILE = $(SRC_PATH)/tests/rotozoom.regression.ref
SEEK_REFFILE     = $(SRC_PATH)/tests/seek.regression.ref

test-server: ffserver$(EXESUF) tests/vsynth1/00.pgm tests/asynth1.sw
	@echo
	@echo "Unfortunately ffserver is broken and therefore its regression"
	@echo "test fails randomly. Treat the results accordingly."
	@echo
	$(SRC_PATH)/tests/server-regression.sh $(FFSERVER_REFFILE) $(SRC_PATH)/tests/test.conf

codectest mpeg4 mpeg ac3 snow snowll: ffmpeg$(EXESUF) tests/vsynth1/00.pgm tests/vsynth2/00.pgm tests/asynth1.sw tests/tiny_psnr$(EXESUF)
	$(SRC_PATH)/tests/regression.sh $@ $(FFMPEG_REFFILE)   tests/vsynth1
	$(SRC_PATH)/tests/regression.sh $@ $(ROTOZOOM_REFFILE) tests/vsynth2

ifeq ($(CONFIG_GPL),yes)
libavtest: ffmpeg$(EXESUF) tests/vsynth1/00.pgm tests/asynth1.sw
	$(SRC_PATH)/tests/regression.sh $@ $(LIBAV_REFFILE) tests/vsynth1
seektest: tests/seek_test$(EXESUF)
	$(SRC_PATH)/tests/seek_test.sh $(SEEK_REFFILE)
else
libavtest seektest:
	@echo
	@echo "This test requires FFmpeg to be compiled with --enable-gpl."
	@echo
	@exit 1
endif

ifeq ($(CONFIG_SWSCALER),yes)
test-server codectest mpeg4 mpeg ac3 snow snowll libavtest: swscale_error
swscale_error:
	@echo
	@echo "This regression test is incompatible with --enable-swscaler."
	@echo
	@exit 1
endif

tests/vsynth1/00.pgm: tests/videogen$(EXESUF)
	mkdir -p tests/vsynth1
	$(BUILD_ROOT)/$< 'tests/vsynth1/'

tests/vsynth2/00.pgm: tests/rotozoom$(EXESUF)
	mkdir -p tests/vsynth2
	$(BUILD_ROOT)/$< 'tests/vsynth2/' $(SRC_PATH)/tests/lena.pnm

tests/asynth1.sw: tests/audiogen$(EXESUF)
	$(BUILD_ROOT)/$< $@

%$(EXESUF): %.c
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $<

tests/seek_test$(EXESUF): tests/seek_test.c
	$(CC) $(LDFLAGS) $(CFLAGS) -DHAVE_AV_CONFIG_H -o $@ $< $(EXTRALIBS)


.PHONY: all lib videohook documentation install* wininstaller uninstall*
.PHONY: dep depend clean distclean TAGS
.PHONY: codectest libavtest seektest test-server fulltest test
.PHONY: mpeg4 mpeg ac3 snow snowll swscale-error

-include .depend
