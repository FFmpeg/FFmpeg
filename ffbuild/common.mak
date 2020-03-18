#
# common bits used by all libraries
#

DEFAULT_X86ASMD=.dbg

ifeq ($(DBG),1)
X86ASMD=$(DEFAULT_X86ASMD)
else
X86ASMD=
endif

ifndef SUBDIR

ifndef V
Q      = @
ECHO   = printf "$(1)\t%s\n" $(2)
BRIEF  = CC CXX OBJCC HOSTCC HOSTLD AS X86ASM AR LD STRIP CP WINDRES NVCC
SILENT = DEPCC DEPHOSTCC DEPAS DEPX86ASM RANLIB RM

MSG    = $@
M      = @$(call ECHO,$(TAG),$@);
$(foreach VAR,$(BRIEF), \
    $(eval override $(VAR) = @$$(call ECHO,$(VAR),$$(MSG)); $($(VAR))))
$(foreach VAR,$(SILENT),$(eval override $(VAR) = @$($(VAR))))
$(eval INSTALL = @$(call ECHO,INSTALL,$$(^:$(SRC_DIR)/%=%)); $(INSTALL))
endif

ALLFFLIBS = avcodec avdevice avfilter avformat avresample avutil postproc swscale swresample

# NASM requires -I path terminated with /
IFLAGS     := -I. -I$(SRC_LINK)/
CPPFLAGS   := $(IFLAGS) $(CPPFLAGS)
CFLAGS     += $(ECFLAGS)
CCFLAGS     = $(CPPFLAGS) $(CFLAGS)
OBJCFLAGS  += $(EOBJCFLAGS)
OBJCCFLAGS  = $(CPPFLAGS) $(CFLAGS) $(OBJCFLAGS)
ASFLAGS    := $(CPPFLAGS) $(ASFLAGS)
CXXFLAGS   := $(CPPFLAGS) $(CFLAGS) $(CXXFLAGS)
X86ASMFLAGS += $(IFLAGS:%=%/) -I$(<D)/ -Pconfig.asm

HOSTCCFLAGS = $(IFLAGS) $(HOSTCPPFLAGS) $(HOSTCFLAGS)
LDFLAGS    := $(ALLFFLIBS:%=$(LD_PATH)lib%) $(LDFLAGS)

define COMPILE
       $(call $(1)DEP,$(1))
       $($(1)) $($(1)FLAGS) $($(1)_DEPFLAGS) $($(1)_C) $($(1)_O) $(patsubst $(SRC_PATH)/%,$(SRC_LINK)/%,$<)
endef

COMPILE_C = $(call COMPILE,CC)
COMPILE_CXX = $(call COMPILE,CXX)
COMPILE_S = $(call COMPILE,AS)
COMPILE_M = $(call COMPILE,OBJCC)
COMPILE_X86ASM = $(call COMPILE,X86ASM)
COMPILE_HOSTC = $(call COMPILE,HOSTCC)
COMPILE_NVCC = $(call COMPILE,NVCC)

%.o: %.c
	$(COMPILE_C)

%.o: %.cpp
	$(COMPILE_CXX)

%.o: %.m
	$(COMPILE_M)

%.s: %.c
	$(CC) $(CCFLAGS) -S -o $@ $<

%.o: %.S
	$(COMPILE_S)

%_host.o: %.c
	$(COMPILE_HOSTC)

%$(DEFAULT_X86ASMD).asm: %.asm
	$(DEPX86ASM) $(X86ASMFLAGS) -M -o $@ $< > $(@:.asm=.d)
	$(X86ASM) $(X86ASMFLAGS) -e $< | sed '/^%/d;/^$$/d;' > $@

%.o: %.asm
	$(COMPILE_X86ASM)
	-$(if $(ASMSTRIPFLAGS), $(STRIP) $(ASMSTRIPFLAGS) $@)

%.o: %.rc
	$(WINDRES) $(IFLAGS) --preprocessor "$(DEPWINDRES) -E -xc-header -DRC_INVOKED $(CC_DEPFLAGS)" -o $@ $<

%.i: %.c
	$(CC) $(CCFLAGS) $(CC_E) $<

%.h.c:
	$(Q)echo '#include "$*.h"' >$@

%.ptx: %.cu $(SRC_PATH)/compat/cuda/cuda_runtime.h
	$(COMPILE_NVCC)

%.ptx.c: %.ptx
	$(Q)sh $(SRC_PATH)/compat/cuda/ptx2c.sh $@ $(patsubst $(SRC_PATH)/%,$(SRC_LINK)/%,$<)

%.c %.h %.pc %.ver %.version: TAG = GEN

# Dummy rule to stop make trying to rebuild removed or renamed headers
%.h:
	@:

# Disable suffix rules.  Most of the builtin rules are suffix rules,
# so this saves some time on slow systems.
.SUFFIXES:

# Do not delete intermediate files from chains of implicit rules
$(OBJS):
endif

include $(SRC_PATH)/ffbuild/arch.mak

OBJS      += $(OBJS-yes)
SLIBOBJS  += $(SLIBOBJS-yes)
FFLIBS    := $($(NAME)_FFLIBS) $(FFLIBS-yes) $(FFLIBS)
TESTPROGS += $(TESTPROGS-yes)

LDLIBS       = $(FFLIBS:%=%$(BUILDSUF))
FFEXTRALIBS := $(LDLIBS:%=$(LD_LIB)) $(foreach lib,EXTRALIBS-$(NAME) $(FFLIBS:%=EXTRALIBS-%),$($(lib))) $(EXTRALIBS)

OBJS      := $(sort $(OBJS:%=$(SUBDIR)%))
SLIBOBJS  := $(sort $(SLIBOBJS:%=$(SUBDIR)%))
TESTOBJS  := $(TESTOBJS:%=$(SUBDIR)tests/%) $(TESTPROGS:%=$(SUBDIR)tests/%.o)
TESTPROGS := $(TESTPROGS:%=$(SUBDIR)tests/%$(EXESUF))
HOSTOBJS  := $(HOSTPROGS:%=$(SUBDIR)%.o)
HOSTPROGS := $(HOSTPROGS:%=$(SUBDIR)%$(HOSTEXESUF))
TOOLS     += $(TOOLS-yes)
TOOLOBJS  := $(TOOLS:%=tools/%.o)
TOOLS     := $(TOOLS:%=tools/%$(EXESUF))
HEADERS   += $(HEADERS-yes)

PATH_LIBNAME = $(foreach NAME,$(1),lib$(NAME)/$($(2)LIBNAME))
DEP_LIBS := $(foreach lib,$(FFLIBS),$(call PATH_LIBNAME,$(lib),$(CONFIG_SHARED:yes=S)))
STATIC_DEP_LIBS := $(foreach lib,$(FFLIBS),$(call PATH_LIBNAME,$(lib)))

SRC_DIR    := $(SRC_PATH)/lib$(NAME)
ALLHEADERS := $(subst $(SRC_DIR)/,$(SUBDIR),$(wildcard $(SRC_DIR)/*.h $(SRC_DIR)/$(ARCH)/*.h))
SKIPHEADERS += $(ARCH_HEADERS:%=$(ARCH)/%) $(SKIPHEADERS-)
SKIPHEADERS := $(SKIPHEADERS:%=$(SUBDIR)%)
HOBJS        = $(filter-out $(SKIPHEADERS:.h=.h.o),$(ALLHEADERS:.h=.h.o))
PTXOBJS      = $(filter %.ptx.o,$(OBJS))
$(HOBJS):     CCFLAGS += $(CFLAGS_HEADERS)
checkheaders: $(HOBJS)
.SECONDARY:   $(HOBJS:.o=.c) $(PTXOBJS:.o=.c) $(PTXOBJS:.o=)

alltools: $(TOOLS)

$(HOSTOBJS): %.o: %.c
	$(COMPILE_HOSTC)

$(HOSTPROGS): %$(HOSTEXESUF): %.o
	$(HOSTLD) $(HOSTLDFLAGS) $(HOSTLD_O) $^ $(HOSTEXTRALIBS)

$(OBJS):     | $(sort $(dir $(OBJS)))
$(HOBJS):    | $(sort $(dir $(HOBJS)))
$(HOSTOBJS): | $(sort $(dir $(HOSTOBJS)))
$(SLIBOBJS): | $(sort $(dir $(SLIBOBJS)))
$(TESTOBJS): | $(sort $(dir $(TESTOBJS)))
$(TOOLOBJS): | tools

OUTDIRS := $(OUTDIRS) $(dir $(OBJS) $(HOBJS) $(HOSTOBJS) $(SLIBOBJS) $(TESTOBJS))

CLEANSUFFIXES     = *.d *.gcda *.gcno *.h.c *.ho *.map *.o *.pc *.ptx *.ptx.c *.ver *.version *$(DEFAULT_X86ASMD).asm *~ *.ilk *.pdb
LIBSUFFIXES       = *.a *.lib *.so *.so.* *.dylib *.dll *.def *.dll.a

define RULES
clean::
	$(RM) $(HOSTPROGS) $(TESTPROGS) $(TOOLS)
endef

$(eval $(RULES))

-include $(wildcard $(OBJS:.o=.d) $(HOSTOBJS:.o=.d) $(TESTOBJS:.o=.d) $(HOBJS:.o=.d) $(SLIBOBJS:.o=.d)) $(OBJS:.o=$(DEFAULT_X86ASMD).d)
