AVPROGS-$(CONFIG_FFMPEG)   += ffmpeg
AVPROGS-$(CONFIG_FFPLAY)   += ffplay
AVPROGS-$(CONFIG_FFPROBE)  += ffprobe

AVPROGS     := $(AVPROGS-yes:%=%$(PROGSSUF)$(EXESUF))
PROGS       += $(AVPROGS)

AVBASENAMES  = ffmpeg ffplay ffprobe
ALLAVPROGS   = $(AVBASENAMES:%=%$(PROGSSUF)$(EXESUF))
ALLAVPROGS_G = $(AVBASENAMES:%=%$(PROGSSUF)_g$(EXESUF))

OBJS-ffmpeg +=                  \
    fftools/ffmpeg_filter.o     \
    fftools/ffmpeg_hw.o         \
    fftools/ffmpeg_mux.o        \
    fftools/ffmpeg_opt.o        \

define DOFFTOOL
OBJS-$(1) += fftools/cmdutils.o fftools/opt_common.o fftools/$(1).o $(OBJS-$(1)-yes)
$(1)$(PROGSSUF)_g$(EXESUF): $$(OBJS-$(1))
$$(OBJS-$(1)): | fftools
$$(OBJS-$(1)): CFLAGS  += $(CFLAGS-$(1))
$(1)$(PROGSSUF)_g$(EXESUF): LDFLAGS += $(LDFLAGS-$(1))
$(1)$(PROGSSUF)_g$(EXESUF): FF_EXTRALIBS += $(EXTRALIBS-$(1))
-include $$(OBJS-$(1):.o=.d)
endef

$(foreach P,$(AVPROGS-yes),$(eval $(call DOFFTOOL,$(P))))

all: $(AVPROGS)

fftools/ffprobe.o fftools/cmdutils.o: libavutil/ffversion.h | fftools
OUTDIRS += fftools

ifdef AVPROGS
install: install-progs install-data
endif

install-progs-yes:
install-progs-$(CONFIG_SHARED): install-libs

install-progs: install-progs-yes $(AVPROGS)
	$(Q)mkdir -p "$(BINDIR)"
	$(INSTALL) -c -m 755 $(AVPROGS) "$(BINDIR)"

uninstall: uninstall-progs

uninstall-progs:
	$(RM) $(addprefix "$(BINDIR)/", $(ALLAVPROGS))

clean::
	$(RM) $(ALLAVPROGS) $(ALLAVPROGS_G) $(CLEANSUFFIXES:%=fftools/%)
