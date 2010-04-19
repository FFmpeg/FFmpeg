include config.mak

SRC_DIR = $(SRC_PATH_BARE)

vpath %.texi $(SRC_PATH_BARE)

PROGS-$(CONFIG_FFMPEG)   += ffmpeg
PROGS-$(CONFIG_FFPLAY)   += ffplay
PROGS-$(CONFIG_FFPROBE)  += ffprobe
PROGS-$(CONFIG_FFSERVER) += ffserver

PROGS      := $(addsuffix   $(EXESUF), $(PROGS-yes))
PROGS_G     = $(addsuffix _g$(EXESUF), $(PROGS-yes))
OBJS        = $(addsuffix .o,          $(PROGS-yes)) cmdutils.o
MANPAGES    = $(addprefix doc/, $(addsuffix .1, $(PROGS-yes)))
TOOLS       = $(addprefix tools/, $(addsuffix $(EXESUF), cws2fws pktdumper probetest qt-faststart trasher))
HOSTPROGS   = $(addprefix tests/, audiogen videogen rotozoom tiny_psnr)

BASENAMES   = ffmpeg ffplay ffprobe ffserver
ALLPROGS    = $(addsuffix   $(EXESUF), $(BASENAMES))
ALLPROGS_G  = $(addsuffix _g$(EXESUF), $(BASENAMES))
ALLMANPAGES = $(addsuffix .1, $(BASENAMES))

FFLIBS-$(CONFIG_AVDEVICE) += avdevice
FFLIBS-$(CONFIG_AVFILTER) += avfilter
FFLIBS-$(CONFIG_AVFORMAT) += avformat
FFLIBS-$(CONFIG_AVCODEC)  += avcodec
FFLIBS-$(CONFIG_POSTPROC) += postproc
FFLIBS-$(CONFIG_SWSCALE)  += swscale

FFLIBS := avutil

DATA_FILES := $(wildcard $(SRC_DIR)/ffpresets/*.ffpreset)

SKIPHEADERS = cmdutils_common_opts.h

include common.mak

FF_LDFLAGS   := $(FFLDFLAGS)
FF_EXTRALIBS := $(FFEXTRALIBS)
FF_DEP_LIBS  := $(DEP_LIBS)

ALL_TARGETS-$(CONFIG_DOC)       += documentation

ifdef PROGS
INSTALL_TARGETS-yes             += install-progs install-data
INSTALL_TARGETS-$(CONFIG_DOC)   += install-man
endif
INSTALL_PROGS_TARGETS-$(CONFIG_SHARED) = install-libs

all: $(FF_DEP_LIBS) $(PROGS) $(ALL_TARGETS-yes)

$(PROGS): %$(EXESUF): %_g$(EXESUF)
	$(CP) $< $@
	$(STRIP) $@

SUBDIR_VARS := OBJS FFLIBS CLEANFILES DIRS TESTPROGS EXAMPLES SKIPHEADERS \
               ALTIVEC-OBJS MMX-OBJS NEON-OBJS X86-OBJS YASM-OBJS-FFT YASM-OBJS \
               HOSTPROGS BUILT_HEADERS TESTOBJS ARCH_HEADERS

define RESET
$(1) :=
$(1)-yes :=
endef

define DOSUBDIR
$(foreach V,$(SUBDIR_VARS),$(eval $(call RESET,$(V))))
SUBDIR := $(1)/
include $(1)/Makefile
endef

$(foreach D,$(FFLIBS),$(eval $(call DOSUBDIR,lib$(D))))

ffplay_g$(EXESUF): FF_EXTRALIBS += $(SDL_LIBS)
ffserver_g$(EXESUF): FF_LDFLAGS += $(FFSERVERLDFLAGS)

%_g$(EXESUF): %.o cmdutils.o $(FF_DEP_LIBS)
	$(LD) $(FF_LDFLAGS) -o $@ $< cmdutils.o $(FF_EXTRALIBS)

tools/%$(EXESUF): tools/%.o
	$(LD) $(FF_LDFLAGS) -o $@ $< $(FF_EXTRALIBS)

tools/%.o: tools/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CC_O) $<

ffplay.o ffplay.d: CFLAGS += $(SDL_CFLAGS)

VERSION_SH  = $(SRC_PATH_BARE)/version.sh
GIT_LOG     = $(SRC_PATH_BARE)/.git/logs/HEAD
SVN_ENTRIES = $(SRC_PATH_BARE)/.svn/entries

.version: $(wildcard $(GIT_LOG) $(SVN_ENTRIES)) $(VERSION_SH) config.mak
.version: M=@

version.h .version:
	$(M)$(VERSION_SH) $(SRC_PATH) version.h $(EXTRA_VERSION)
	$(Q)touch .version

# force version.sh to run whenever version might have changed
-include .version

alltools: $(TOOLS)

documentation: $(addprefix doc/, developer.html faq.html ffmpeg-doc.html \
                                 ffplay-doc.html ffprobe-doc.html ffserver-doc.html \
                                 general.html libavfilter.html $(ALLMANPAGES))

doc/%.html: TAG = HTML
doc/%.html: doc/%.texi
	$(M)cd doc && texi2html -monolithic -number $(<:doc/%=%)

doc/%.pod: TAG = POD
doc/%.pod: doc/%-doc.texi
	$(M)doc/texi2pod.pl $< $@

doc/%.1: TAG = MAN
doc/%.1: doc/%.pod
	$(M)pod2man --section=1 --center=" " --release=" " $< > $@

install: $(INSTALL_TARGETS-yes)

install-progs: $(PROGS) $(INSTALL_PROGS_TARGETS-yes)
	$(Q)mkdir -p "$(BINDIR)"
	$(INSTALL) -c -m 755 $(PROGS) "$(BINDIR)"

install-data: $(DATA_FILES)
	$(Q)mkdir -p "$(DATADIR)"
	$(INSTALL) -m 644 $(DATA_FILES) "$(DATADIR)"

install-man: $(MANPAGES)
	$(Q)mkdir -p "$(MANDIR)/man1"
	$(INSTALL) -m 644 $(MANPAGES) "$(MANDIR)/man1"

uninstall: uninstall-progs uninstall-data uninstall-man

uninstall-progs:
	$(RM) $(addprefix "$(BINDIR)/", $(ALLPROGS))

uninstall-data:
	$(RM) -r "$(DATADIR)"

uninstall-man:
	$(RM) $(addprefix "$(MANDIR)/man1/",$(ALLMANPAGES))

testclean:
	$(RM) -r tests/vsynth1 tests/vsynth2 tests/data
	$(RM) $(addprefix tests/,$(CLEANSUFFIXES))
	$(RM) tests/seek_test$(EXESUF) tests/seek_test.o
	$(RM) $(addprefix tests/,$(addsuffix $(HOSTEXESUF),audiogen videogen rotozoom tiny_psnr))

clean:: testclean
	$(RM) $(ALLPROGS) $(ALLPROGS_G)
	$(RM) $(CLEANSUFFIXES)
	$(RM) doc/*.html doc/*.pod doc/*.1
	$(RM) $(TOOLS)

distclean::
	$(RM) $(DISTCLEANSUFFIXES)
	$(RM) version.h config.* libavutil/avconfig.h

config:
	$(SRC_PATH)/configure $(value FFMPEG_CONFIGURATION)

# regression tests

check: test checkheaders

fulltest test: codectest lavftest seektest

FFSERVER_REFFILE = $(SRC_PATH)/tests/ffserver.regression.ref
SEEK_REFFILE     = $(SRC_PATH)/tests/seek.regression.ref

ENCDEC = $(and $(CONFIG_$(1)_ENCODER),$(CONFIG_$(1)_DECODER))
MUXDEM = $(and $(CONFIG_$(1)_MUXER),$(CONFIG_$(or $(2),$(1))_DEMUXER))

VCODEC_TESTS =
VCODEC_TESTS-$(call ENCDEC,ASV1)             += asv1
VCODEC_TESTS-$(call ENCDEC,ASV2)             += asv2
VCODEC_TESTS-$(call ENCDEC,DNXHD)            += dnxhd_1080i dnxhd_720p dnxhd_720p_rd
VCODEC_TESTS-$(call ENCDEC,DVVIDEO)          += dv dv50
VCODEC_TESTS-$(call ENCDEC,FFV1)             += ffv1
VCODEC_TESTS-$(call ENCDEC,FLASHSV)          += flashsv
VCODEC_TESTS-$(call ENCDEC,FLV)              += flv
VCODEC_TESTS-$(call ENCDEC,H261)             += h261
VCODEC_TESTS-$(call ENCDEC,H263)             += h263 h263p
VCODEC_TESTS-$(call ENCDEC,HUFFYUV)          += huffyuv
VCODEC_TESTS-$(call ENCDEC,JPEGLS)           += jpegls
VCODEC_TESTS-$(call ENCDEC,MJPEG)            += mjpeg ljpeg
VCODEC_TESTS-$(call ENCDEC,MPEG1VIDEO)       += mpeg mpeg1b
VCODEC_TESTS-$(call ENCDEC,MPEG2VIDEO)       += mpeg2 mpeg2thread
VCODEC_TESTS-$(call ENCDEC,MPEG4)            += mpeg4 mpeg4adv mpeg4nr mpeg4thread error rc
VCODEC_TESTS-$(call ENCDEC,MSMPEG4V1)        += msmpeg4
VCODEC_TESTS-$(call ENCDEC,MSMPEG4V2)        += msmpeg4v2
VCODEC_TESTS-$(call ENCDEC,ROQ)              += roq
VCODEC_TESTS-$(call ENCDEC,RV10)             += rv10
VCODEC_TESTS-$(call ENCDEC,RV20)             += rv20
VCODEC_TESTS-$(call ENCDEC,SNOW)             += snow snowll
VCODEC_TESTS-$(call ENCDEC,SVQ1)             += svq1
VCODEC_TESTS-$(call ENCDEC,WMV1)             += wmv1
VCODEC_TESTS-$(call ENCDEC,WMV2)             += wmv2

ACODEC_TESTS =
ACODEC_TESTS-$(call ENCDEC,AC3)              += ac3
ACODEC_TESTS-$(call ENCDEC,ADPCM_G726)       += g726
ACODEC_TESTS-$(call ENCDEC,ADPCM_IMA_QT)     += adpcm_ima_qt
ACODEC_TESTS-$(call ENCDEC,ADPCM_IMA_WAV)    += adpcm_ima_wav
ACODEC_TESTS-$(call ENCDEC,ADPCM_MS)         += adpcm_ms
ACODEC_TESTS-$(call ENCDEC,ADPCM_SWF)        += adpcm_swf
ACODEC_TESTS-$(call ENCDEC,ADPCM_YAMAHA)     += adpcm_yam
ACODEC_TESTS-$(call ENCDEC,ALAC)             += alac
ACODEC_TESTS-$(call ENCDEC,FLAC)             += flac
ACODEC_TESTS-$(call ENCDEC,MP2)              += mp2
ACODEC_TESTS-$(call ENCDEC,PCM_S16LE)        += pcm         # fixme
ACODEC_TESTS-$(call ENCDEC,WMAV1)            += wmav1
ACODEC_TESTS-$(call ENCDEC,WMAV1)            += wmav2

LAVF_TESTS =
LAVF_TESTS-$(call MUXDEM,AIFF)               += aiff
LAVF_TESTS-$(call MUXDEM,PCM_ALAW)           += alaw
LAVF_TESTS-$(call MUXDEM,ASF)                += asf
LAVF_TESTS-$(call MUXDEM,AU)                 += au
LAVF_TESTS-$(call MUXDEM,AVI)                += avi
LAVF_TESTS-$(call ENCDEC,BMP)                += bmp
LAVF_TESTS-$(call MUXDEM,DV)                 += dv_fmt
LAVF_TESTS-$(call MUXDEM,FFM)                += ffm
LAVF_TESTS-$(call MUXDEM,FLV)                += flv_fmt
LAVF_TESTS-$(call ENCDEC,GIF)                += gif
LAVF_TESTS-$(call MUXDEM,GXF)                += gxf
LAVF_TESTS-$(call ENCDEC,MJPEG)              += jpg
LAVF_TESTS-$(call MUXDEM,MATROSKA)           += mkv
LAVF_TESTS-$(call MUXDEM,MMF)                += mmf
LAVF_TESTS-$(call MUXDEM,MOV)                += mov
LAVF_TESTS-$(call MUXDEM,MPEG1SYSTEM,MPEGPS) += mpg
LAVF_TESTS-$(call MUXDEM,PCM_MULAW)          += mulaw
LAVF_TESTS-$(call MUXDEM,MXF)                += mxf
LAVF_TESTS-$(call MUXDEM,NUT)                += nut
LAVF_TESTS-$(call MUXDEM,OGG)                += ogg
LAVF_TESTS-$(call ENCDEC,PBM)                += pbmpipe
LAVF_TESTS-$(call ENCDEC,PCX)                += pcx
LAVF_TESTS-$(call ENCDEC,PGM)                += pgm pgmpipe
LAVF_TESTS-$(call MUXDEM,RAWVIDEO)           += pixfmt
LAVF_TESTS-$(call ENCDEC,PPM)                += ppm ppmpipe
LAVF_TESTS-$(call MUXDEM,RM)                 += rm
LAVF_TESTS-$(call ENCDEC,SGI)                += sgi
LAVF_TESTS-$(call MUXDEM,SWF)                += swf
LAVF_TESTS-$(call ENCDEC,TARGA)              += tga
LAVF_TESTS-$(call ENCDEC,TIFF)               += tiff
LAVF_TESTS-$(call MUXDEM,MPEGTS)             += ts
LAVF_TESTS-$(call MUXDEM,VOC)                += voc
LAVF_TESTS-$(call MUXDEM,WAV)                += wav
LAVF_TESTS-$(call MUXDEM,YUV4MPEGPIPE)       += yuv4mpeg

LAVFI_TESTS =           \
    crop                \
    crop_scale          \
    crop_scale_vflip    \
    crop_vflip          \
    null                \
    scale200            \
    scale500            \
    vflip               \
    vflip_crop          \
    vflip_vflip         \

ACODEC_TESTS := $(addprefix regtest-, $(ACODEC_TESTS) $(ACODEC_TESTS-yes))
VCODEC_TESTS := $(addprefix regtest-, $(VCODEC_TESTS) $(VCODEC_TESTS-yes))
LAVF_TESTS  := $(addprefix regtest-, $(LAVF_TESTS)  $(LAVF_TESTS-yes))
LAVFI_TESTS := $(addprefix regtest-, $(LAVFI_TESTS) $(LAVFI_TESTS-yes))

CODEC_TESTS = $(VCODEC_TESTS) $(ACODEC_TESTS)

codectest: $(CODEC_TESTS)
lavftest:  $(LAVF_TESTS)
lavfitest: $(LAVFI_TESTS)

$(ACODEC_TESTS): regtest-aref
$(VCODEC_TESTS): regtest-vref
$(LAVF_TESTS) $(LAVFI_TESTS): regtest-ref

REFFILE = $(SRC_PATH)/tests/ref/$(1)/$(2:regtest-%=%)
RESFILE = tests/data/$(2:regtest-%=%).$(1).regression

define CODECTEST_CMD
	$(SRC_PATH)/tests/codec-regression.sh $@ vsynth1 tests/vsynth1 "$(TARGET_EXEC)" "$(TARGET_PATH)"
	$(SRC_PATH)/tests/codec-regression.sh $@ vsynth2 tests/vsynth2 "$(TARGET_EXEC)" "$(TARGET_PATH)"
endef

regtest-ref: regtest-aref regtest-vref

regtest-vref: ffmpeg$(EXESUF) tests/vsynth1/00.pgm tests/vsynth2/00.pgm
	$(CODECTEST_CMD)

regtest-aref: ffmpeg$(EXESUF) tests/data/asynth1.sw
	@$(SRC_PATH)/tests/codec-regression.sh $@ acodec tests/acodec "$(TARGET_EXEC)" "$(TARGET_PATH)"

$(VCODEC_TESTS): tests/tiny_psnr$(HOSTEXESUF)
	@echo "TEST VCODEC $(@:regtest-%=%)"
	@$(CODECTEST_CMD)
	@diff -u -w $(call REFFILE,vsynth1,$@) $(call RESFILE,vsynth1,$@)
	@diff -u -w $(call REFFILE,vsynth2,$@) $(call RESFILE,vsynth2,$@)

$(ACODEC_TESTS): tests/tiny_psnr$(HOSTEXESUF)
	@echo "TEST ACODEC $(@:regtest-%=%)"
	@$(SRC_PATH)/tests/codec-regression.sh $@ acodec tests/acodec "$(TARGET_EXEC)" "$(TARGET_PATH)"
	@diff -u -w $(call REFFILE,acodec,$@) $(call RESFILE,acodec,$@)

$(LAVF_TESTS):
	@echo "TEST LAVF  $(@:regtest-%=%)"
	@$(SRC_PATH)/tests/lavf-regression.sh $@ lavf tests/vsynth1 "$(TARGET_EXEC)" "$(TARGET_PATH)"
	@diff -u -w $(call REFFILE,lavf,$@) $(call RESFILE,lavf,$@)

$(LAVFI_TESTS):
	@echo "TEST LAVFI $(@:regtest-%=%)"
	@$(SRC_PATH)/tests/lavfi-regression.sh $@ lavfi tests/vsynth1 "$(TARGET_EXEC)" "$(TARGET_PATH)"
	@diff -u -w $(call REFFILE,lavfi,$@) $(call RESFILE,lavfi,$@)

seektest: codectest lavftest tests/seek_test$(EXESUF)
	$(SRC_PATH)/tests/seek-regression.sh $(SRC_PATH) "$(TARGET_EXEC)" "$(TARGET_PATH)"

ffservertest: ffserver$(EXESUF) tests/vsynth1/00.pgm tests/data/asynth1.sw
	@echo
	@echo "Unfortunately ffserver is broken and therefore its regression"
	@echo "test fails randomly. Treat the results accordingly."
	@echo
	$(SRC_PATH)/tests/ffserver-regression.sh $(FFSERVER_REFFILE) $(SRC_PATH)/tests/ffserver.conf

tests/vsynth1/00.pgm: tests/videogen$(HOSTEXESUF)
	mkdir -p tests/vsynth1
	$(BUILD_ROOT)/$< 'tests/vsynth1/'

tests/vsynth2/00.pgm: tests/rotozoom$(HOSTEXESUF)
	mkdir -p tests/vsynth2
	$(BUILD_ROOT)/$< 'tests/vsynth2/' $(SRC_PATH)/tests/lena.pnm

tests/data/asynth1.sw: tests/audiogen$(HOSTEXESUF)
	mkdir -p tests/data
	$(BUILD_ROOT)/$< $@

tests/seek_test$(EXESUF): tests/seek_test.o $(FF_DEP_LIBS)
	$(LD) $(FF_LDFLAGS) -o $@ $< $(FF_EXTRALIBS)

ifdef SAMPLES
include $(SRC_PATH_BARE)/tests/fate.mak
fate: $(FATE_TESTS)
$(FATE_TESTS): ffmpeg$(EXESUF)
	@echo "TEST FATE   $(@:fate-%=%)"
	@$(SRC_PATH)/tests/fate-run.sh $@ "$(SAMPLES)" "$(TARGET_EXEC)" "$(TARGET_PATH)" '$(CMD)'
else
fate:
	@echo "SAMPLES not specified, cannot run FATE"
endif

.PHONY: documentation *test regtest-* zlib-error alltools check config
