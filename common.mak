#
# common bits used by all libraries
#

# first so "all" becomes default target
all: all-yes

ifndef SUBDIR

ifndef V
Q      = @
ECHO   = printf "$(1)\t%s\n" $(2)
BRIEF  = CC CXX AS YASM AR LD HOSTCC STRIP CP
SILENT = DEPCC YASMDEP RM RANLIB
MSG    = $@
M      = @$(call ECHO,$(TAG),$@);
$(foreach VAR,$(BRIEF), \
    $(eval override $(VAR) = @$$(call ECHO,$(VAR),$$(MSG)); $($(VAR))))
$(foreach VAR,$(SILENT),$(eval override $(VAR) = @$($(VAR))))
$(eval INSTALL = @$(call ECHO,INSTALL,$$(^:$(SRC_DIR)/%=%)); $(INSTALL))
endif

ALLFFLIBS = avcodec avdevice avfilter avformat avutil postproc swscale swresample

# NASM requires -I path terminated with /
IFLAGS     := -I. -I$(SRC_PATH)/
CPPFLAGS   := $(IFLAGS) $(CPPFLAGS)
CFLAGS     += $(ECFLAGS)
CCFLAGS     = $(CFLAGS)
CXXFLAGS   := $(CFLAGS) $(CXXFLAGS)
YASMFLAGS  += $(IFLAGS) -I$(SRC_PATH)/libavutil/x86/ -Pconfig.asm
HOSTCFLAGS += $(IFLAGS)
LDFLAGS    := $(ALLFFLIBS:%=-Llib%) $(LDFLAGS)

define COMPILE
       $($(1)DEP)
       $($(1)) $(CPPFLAGS) $($(1)FLAGS) $($(1)_DEPFLAGS) -c $($(1)_O) $<
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

FFEXTRALIBS := $(FFLIBS:%=-l%$(BUILDSUF)) $(EXTRALIBS)

EXAMPLES  := $(EXAMPLES:%=$(SUBDIR)%-example$(EXESUF))
OBJS      := $(sort $(OBJS:%=$(SUBDIR)%))
TESTOBJS  := $(TESTOBJS:%=$(SUBDIR)%) $(TESTPROGS:%=$(SUBDIR)%-test.o)
TESTPROGS := $(TESTPROGS:%=$(SUBDIR)%-test$(EXESUF))
HOSTOBJS  := $(HOSTPROGS:%=$(SUBDIR)%.o)
HOSTPROGS := $(HOSTPROGS:%=$(SUBDIR)%$(HOSTEXESUF))
TOOLS     += $(TOOLS-yes)
TOOLOBJS  := $(TOOLS:%=tools/%.o)
TOOLS     := $(TOOLS:%=tools/%$(EXESUF))

DEP_LIBS := $(foreach NAME,$(FFLIBS),lib$(NAME)/$($(CONFIG_SHARED:yes=S)LIBNAME))

ALLHEADERS := $(subst $(SRC_DIR)/,$(SUBDIR),$(wildcard $(SRC_DIR)/*.h $(SRC_DIR)/$(ARCH)/*.h))
SKIPHEADERS += $(ARCH_HEADERS:%=$(ARCH)/%) $(SKIPHEADERS-)
SKIPHEADERS := $(SKIPHEADERS:%=$(SUBDIR)%)
checkheaders: $(filter-out $(SKIPHEADERS:.h=.ho),$(ALLHEADERS:.h=.ho))

alltools: $(TOOLS)

$(HOSTOBJS): %.o: %.c
	$(HOSTCC) $(HOSTCFLAGS) -c -o $@ $<

$(HOSTPROGS): %$(HOSTEXESUF): %.o
	$(HOSTCC) $(HOSTLDFLAGS) -o $@ $< $(HOSTLIBS)

$(OBJS):     | $(sort $(dir $(OBJS)))
$(HOSTOBJS): | $(sort $(dir $(HOSTOBJS)))
$(TESTOBJS): | $(sort $(dir $(TESTOBJS)))
$(TOOLOBJS): | tools

OBJDIRS := $(OBJDIRS) $(dir $(OBJS) $(HOSTOBJS) $(TESTOBJS))

CLEANSUFFIXES     = *.d *.o *~ *.ho *.map *.ver
DISTCLEANSUFFIXES = *.pc
LIBSUFFIXES       = *.a *.lib *.so *.so.* *.dylib *.dll *.def *.dll.a *.exp

-include $(wildcard $(OBJS:.o=.d) $(TESTOBJS:.o=.d))
