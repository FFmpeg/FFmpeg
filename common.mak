#
# common bits used by all libraries
#

# first so "all" becomes default target
all: all-yes

ifndef SUBDIR

ifndef V
Q      = @
ECHO   = printf "$(1)\t%s\n" $(2)
BRIEF  = CC AS YASM AR LD HOSTCC STRIP CP
SILENT = DEPCC YASMDEP RM RANLIB
MSG    = $@
M      = @$(call ECHO,$(TAG),$@);
$(foreach VAR,$(BRIEF), \
    $(eval override $(VAR) = @$$(call ECHO,$(VAR),$$(MSG)); $($(VAR))))
$(foreach VAR,$(SILENT),$(eval override $(VAR) = @$($(VAR))))
$(eval INSTALL = @$(call ECHO,INSTALL,$$(^:$(SRC_DIR)/%=%)); $(INSTALL))
endif

IFLAGS   := -I. -I$(SRC_PATH)
CPPFLAGS := $(IFLAGS) $(CPPFLAGS)
CFLAGS   += $(ECFLAGS)
YASMFLAGS += $(IFLAGS) -Pconfig.asm

HOSTCFLAGS += $(IFLAGS)

%.o: %.c
	$(CCDEP)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CC_DEPFLAGS) -c $(CC_O) $<

%.o: %.S
	$(ASDEP)
	$(AS) $(CPPFLAGS) $(ASFLAGS) $(AS_DEPFLAGS) -c -o $@ $<

%.ho: %.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-unused -c -o $@ -x c $<

%.ver: %.v
	$(Q)sed 's/$$MAJOR/$($(basename $(@F))_VERSION_MAJOR)/' $^ > $@

%.c %.h: TAG = GEN

# Dummy rule to stop make trying to rebuild removed or renamed headers
%.h:
	@:

# Disable suffix rules.  Most of the builtin rules are suffix rules,
# so this saves some time on slow systems.
.SUFFIXES:

# Do not delete intermediate files from chains of implicit rules
$(OBJS):
endif

OBJS-$(HAVE_MMX) +=  $(MMX-OBJS-yes)

OBJS      += $(OBJS-yes)
FFLIBS    := $(FFLIBS-yes) $(FFLIBS)
TESTPROGS += $(TESTPROGS-yes)

FFEXTRALIBS := $(addprefix -l,$(addsuffix $(BUILDSUF),$(FFLIBS))) $(EXTRALIBS)
FFLDFLAGS   := $(addprefix -Llib,$(ALLFFLIBS)) $(LDFLAGS)

EXAMPLES  := $(addprefix $(SUBDIR),$(addsuffix -example$(EXESUF),$(EXAMPLES)))
OBJS      := $(addprefix $(SUBDIR),$(sort $(OBJS)))
TESTOBJS  := $(addprefix $(SUBDIR),$(TESTOBJS) $(TESTPROGS:%=%-test.o))
TESTPROGS := $(addprefix $(SUBDIR),$(addsuffix -test$(EXESUF),$(TESTPROGS)))
HOSTOBJS  := $(addprefix $(SUBDIR),$(addsuffix .o,$(HOSTPROGS)))
HOSTPROGS := $(addprefix $(SUBDIR),$(addsuffix $(HOSTEXESUF),$(HOSTPROGS)))

DEP_LIBS := $(foreach NAME,$(FFLIBS),lib$(NAME)/$($(CONFIG_SHARED:yes=S)LIBNAME))

ALLHEADERS := $(subst $(SRC_DIR)/,$(SUBDIR),$(wildcard $(SRC_DIR)/*.h $(SRC_DIR)/$(ARCH)/*.h))
SKIPHEADERS += $(addprefix $(ARCH)/,$(ARCH_HEADERS))
SKIPHEADERS := $(addprefix $(SUBDIR),$(SKIPHEADERS-) $(SKIPHEADERS))
checkheaders: $(filter-out $(SKIPHEADERS:.h=.ho),$(ALLHEADERS:.h=.ho))

$(HOSTOBJS): %.o: %.c
	$(HOSTCC) $(HOSTCFLAGS) -c -o $@ $<

$(HOSTPROGS): %$(HOSTEXESUF): %.o
	$(HOSTCC) $(HOSTLDFLAGS) -o $@ $< $(HOSTLIBS)

CLEANSUFFIXES     = *.d *.o *~ *.ho *.map *.ver
DISTCLEANSUFFIXES = *.pc
LIBSUFFIXES       = *.a *.lib *.so *.so.* *.dylib *.dll *.def *.dll.a *.exp

-include $(wildcard $(OBJS:.o=.d) $(TESTOBJS:.o=.d))
