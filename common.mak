#
# common bits used by all libraries
#

# first so "all" becomes default target
all: all-yes

ifndef SUBDIR

ifndef V
Q      = @
ECHO   = printf "$(1)\t%s\n" $(2)
BRIEF  = CC CXX HOSTCC HOSTLD AS YASM AR LD STRIP CP
SILENT = DEPCC DEPHOSTCC DEPAS DEPYASM RANLIB RM

MSG    = $@
M      = @$(call ECHO,$(TAG),$@);
$(foreach VAR,$(BRIEF), \
    $(eval override $(VAR) = @$$(call ECHO,$(VAR),$$(MSG)); $($(VAR))))
$(foreach VAR,$(SILENT),$(eval override $(VAR) = @$($(VAR))))
$(eval INSTALL = @$(call ECHO,INSTALL,$$(^:$(SRC_DIR)/%=%)); $(INSTALL))
endif

ALLFFLIBS = avcodec avdevice avfilter avformat avresample avutil postproc swscale swresample

# NASM requires -I path terminated with /
IFLAGS     := -I. -I$(SRC_PATH)/
CPPFLAGS   := $(IFLAGS) $(CPPFLAGS)
CFLAGS     += $(ECFLAGS)
CCFLAGS     = $(CPPFLAGS) $(CFLAGS)
ASFLAGS    := $(CPPFLAGS) $(ASFLAGS)
CXXFLAGS   += $(CPPFLAGS) $(CFLAGS)
YASMFLAGS  += $(IFLAGS:%=%/) -Pconfig.asm

HOSTCCFLAGS = $(IFLAGS) $(HOSTCFLAGS)
LDFLAGS    := $(ALLFFLIBS:%=$(LD_PATH)lib%) $(LDFLAGS)

define COMPILE
       $(call $(1)DEP,$(1))
       $($(1)) $($(1)FLAGS) $($(1)_DEPFLAGS) $($(1)_C) $($(1)_O) $<
endef

COMPILE_C = $(call COMPILE,CC)
COMPILE_CXX = $(call COMPILE,CXX)
COMPILE_S = $(call COMPILE,AS)

%.o: %.c
	$(COMPILE_C)

%.o: %.cpp
	$(COMPILE_CXX)

%.s: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -S -o $@ $<

%.o: %.S
	$(COMPILE_S)

%.i: %.c
	$(CC) $(CCFLAGS) $(CC_E) $<

%.h.c:
	$(Q)echo '#include "$*.h"' >$@

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

include $(SRC_PATH)/arch.mak

OBJS      += $(OBJS-yes)
FFLIBS    := $(FFLIBS-yes) $(FFLIBS)
TESTPROGS += $(TESTPROGS-yes)

LDLIBS       = $(FFLIBS:%=%$(BUILDSUF))
FFEXTRALIBS := $(LDLIBS:%=$(LD_LIB)) $(EXTRALIBS)

EXAMPLES  := $(EXAMPLES:%=$(SUBDIR)%-example$(EXESUF))
OBJS      := $(sort $(OBJS:%=$(SUBDIR)%))
TESTOBJS  := $(TESTOBJS:%=$(SUBDIR)%) $(TESTPROGS:%=$(SUBDIR)%-test.o)
TESTPROGS := $(TESTPROGS:%=$(SUBDIR)%-test$(EXESUF))
HOSTOBJS  := $(HOSTPROGS:%=$(SUBDIR)%.o)
HOSTPROGS := $(HOSTPROGS:%=$(SUBDIR)%$(HOSTEXESUF))
TOOLS     += $(TOOLS-yes)
TOOLOBJS  := $(TOOLS:%=tools/%.o)
TOOLS     := $(TOOLS:%=tools/%$(EXESUF))
HEADERS   += $(HEADERS-yes)

DEP_LIBS := $(foreach NAME,$(FFLIBS),lib$(NAME)/$($(CONFIG_SHARED:yes=S)LIBNAME))

ALLHEADERS := $(subst $(SRC_DIR)/,$(SUBDIR),$(wildcard $(SRC_DIR)/*.h $(SRC_DIR)/$(ARCH)/*.h))
SKIPHEADERS += $(ARCH_HEADERS:%=$(ARCH)/%) $(SKIPHEADERS-)
SKIPHEADERS := $(SKIPHEADERS:%=$(SUBDIR)%)
HOBJS        = $(filter-out $(SKIPHEADERS:.h=.h.o),$(ALLHEADERS:.h=.h.o))
checkheaders: $(HOBJS)
.SECONDARY:   $(HOBJS:.o=.c)

alltools: $(TOOLS)

$(HOSTOBJS): %.o: %.c
	$(call COMPILE,HOSTCC)

$(HOSTPROGS): %$(HOSTEXESUF): %.o
	$(HOSTLD) $(HOSTLDFLAGS) $(HOSTLD_O) $< $(HOSTLIBS)

$(OBJS):     | $(sort $(dir $(OBJS)))
$(HOSTOBJS): | $(sort $(dir $(HOSTOBJS)))
$(TESTOBJS): | $(sort $(dir $(TESTOBJS)))
$(HOBJS):    | $(sort $(dir $(HOBJS)))
$(TOOLOBJS): | tools

OBJDIRS := $(OBJDIRS) $(dir $(OBJS) $(HOSTOBJS) $(TESTOBJS) $(HOBJS))

CLEANSUFFIXES     = *.d *.o *~ *.h.c *.map *.ver *.ho *.gcno *.gcda
DISTCLEANSUFFIXES = *.pc
LIBSUFFIXES       = *.a *.lib *.so *.so.* *.dylib *.dll *.def *.dll.a

define RULES
clean::
	$(RM) $(OBJS) $(OBJS:.o=.d)
	$(RM) $(HOSTPROGS)
	$(RM) $(TOOLS)
endef

$(eval $(RULES))

-include $(wildcard $(OBJS:.o=.d) $(HOSTOBJS:.o=.d) $(TESTOBJS:.o=.d) $(HOBJS:.o=.d))
