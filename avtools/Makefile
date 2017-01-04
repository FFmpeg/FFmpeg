AVPROGS-$(CONFIG_AVCONV)   += avconv
AVPROGS-$(CONFIG_AVPLAY)   += avplay
AVPROGS-$(CONFIG_AVPROBE)  += avprobe

AVPROGS    := $(AVPROGS-yes:%=%$(EXESUF))
PROGS      += $(AVPROGS)

AVBASENAMES = avconv avplay avprobe
ALLAVPROGS  = $(AVBASENAMES:%=%$(EXESUF))

OBJS-avconv                   += avtools/avconv_opt.o avtools/avconv_filter.o
OBJS-avconv-$(CONFIG_LIBMFX)  += avtools/avconv_qsv.o
OBJS-avconv-$(CONFIG_VAAPI)   += avtools/avconv_vaapi.o
OBJS-avconv-$(CONFIG_VDA)     += avtools/avconv_vda.o
OBJS-avconv-$(HAVE_DXVA2_LIB) += avtools/avconv_dxva2.o
OBJS-avconv-$(HAVE_VDPAU_X11) += avtools/avconv_vdpau.o

define DOAVTOOL
OBJS-$(1) += avtools/cmdutils.o avtools/$(1).o $(OBJS-$(1)-yes)
$(1)$(EXESUF): $$(OBJS-$(1))
$$(OBJS-$(1)): | avtools
$$(OBJS-$(1)): CFLAGS  += $(CFLAGS-$(1))
$(1)$(EXESUF): LDFLAGS += $(LDFLAGS-$(1))
$(1)$(EXESUF): FF_EXTRALIBS += $(EXTRALIBS-$(1))
-include $$(OBJS-$(1):.o=.d)
endef

$(foreach P,$(AVPROGS-yes),$(eval $(call DOAVTOOL,$(P))))

all: $(AVPROGS)

avtools/cmdutils.o: avversion.h | avtools
OBJDIRS += avtools

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
	$(RM) $(ALLAVPROGS) $(CLEANSUFFIXES:%=avtools/%)
