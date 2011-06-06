include config.mak

SRC_DIR = $(SRC_PATH_BARE)

vpath %.c   $(SRC_DIR)
vpath %.h   $(SRC_DIR)
vpath %.S   $(SRC_DIR)
vpath %.asm $(SRC_DIR)
vpath %.v   $(SRC_DIR)
vpath %.texi $(SRC_PATH_BARE)

PROGS-$(CONFIG_FFMPEG)   += ffmpeg
PROGS-$(CONFIG_FFPLAY)   += ffplay
PROGS-$(CONFIG_FFPROBE)  += ffprobe
PROGS-$(CONFIG_FFSERVER) += ffserver

PROGS      := $(PROGS-yes:%=%$(EXESUF))
OBJS        = $(PROGS-yes:%=%.o) cmdutils.o
TOOLS       = $(addprefix tools/, $(addsuffix $(EXESUF), cws2fws graph2dot lavfi-showfiltfmts pktdumper probetest qt-faststart trasher))
TESTTOOLS   = audiogen videogen rotozoom tiny_psnr base64
HOSTPROGS  := $(TESTTOOLS:%=tests/%)

BASENAMES   = ffmpeg ffplay ffprobe ffserver
ALLPROGS    = $(BASENAMES:%=%$(EXESUF))
ALLMANPAGES = $(BASENAMES:%=%.1)

ALLFFLIBS = avcodec avdevice avfilter avformat avutil postproc swscale

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

all: $(FF_DEP_LIBS) $(PROGS)

config.h: .config
.config: $(wildcard $(FFLIBS:%=$(SRC_DIR)/lib%/all*.c))
	@-tput bold 2>/dev/null
	@-printf '\nWARNING: $(?F) newer than config.h, rerun configure\n\n'
	@-tput sgr0 2>/dev/null

SUBDIR_VARS := OBJS FFLIBS CLEANFILES DIRS TESTPROGS EXAMPLES SKIPHEADERS \
               ALTIVEC-OBJS MMX-OBJS NEON-OBJS X86-OBJS YASM-OBJS-FFT YASM-OBJS \
               HOSTPROGS BUILT_HEADERS TESTOBJS ARCH_HEADERS ARMV6-OBJS

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

ffplay.o: CFLAGS += $(SDL_CFLAGS)
ffplay$(EXESUF): FF_EXTRALIBS += $(SDL_LIBS)
ffserver$(EXESUF): FF_LDFLAGS += $(FFSERVERLDFLAGS)

$(PROGS): %$(EXESUF): %.o cmdutils.o $(FF_DEP_LIBS)
	$(LD) $(FF_LDFLAGS) -o $@ $< cmdutils.o $(FF_EXTRALIBS)

alltools: $(TOOLS)

tools/%$(EXESUF): tools/%.o
	$(LD) $(FF_LDFLAGS) -o $@ $< $(FF_EXTRALIBS)

tools/%.o: tools/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(CC_O) $<

-include $(wildcard tools/*.d)

VERSION_SH  = $(SRC_PATH_BARE)/version.sh
GIT_LOG     = $(SRC_PATH_BARE)/.git/logs/HEAD

.version: $(wildcard $(GIT_LOG)) $(VERSION_SH) config.mak
.version: M=@

version.h .version:
	$(M)$(VERSION_SH) $(SRC_PATH) version.h $(EXTRA_VERSION)
	$(Q)touch .version

# force version.sh to run whenever version might have changed
-include .version

ifdef PROGS
install: install-progs install-data
endif

install: install-libs install-headers

install-libs: install-libs-yes

install-progs-yes:
install-progs-$(CONFIG_SHARED): install-libs

install-progs: install-progs-yes $(PROGS)
	$(Q)mkdir -p "$(BINDIR)"
	$(INSTALL) -c -m 755 $(PROGS) "$(BINDIR)"

install-data: $(DATA_FILES)
	$(Q)mkdir -p "$(DATADIR)"
	$(INSTALL) -m 644 $(DATA_FILES) "$(DATADIR)"

uninstall: uninstall-libs uninstall-headers uninstall-progs uninstall-data

uninstall-progs:
	$(RM) $(addprefix "$(BINDIR)/", $(ALLPROGS))

uninstall-data:
	$(RM) -r "$(DATADIR)"

clean::
	$(RM) $(ALLPROGS)
	$(RM) $(CLEANSUFFIXES)
	$(RM) $(TOOLS)
	$(RM) $(CLEANSUFFIXES:%=tools/%)

distclean::
	$(RM) $(DISTCLEANSUFFIXES)
	$(RM) config.* .version version.h libavutil/avconfig.h

config:
	$(SRC_PATH)/configure $(value LIBAV_CONFIGURATION)

check: test checkheaders

include doc/Makefile
include tests/Makefile

.PHONY: all alltools *clean check config examples install*
.PHONY: testprogs uninstall*
