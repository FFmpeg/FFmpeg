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

BIN2CEXE = ffbuild/bin2c$(HOSTEXESUF)
BIN2C = $(BIN2CEXE)

ifndef V
Q      = @
ECHO   = printf "$(1)\t%s\n" $(2)
BRIEF  = CC CXX OBJCC HOSTCC HOSTLD AS X86ASM AR LD STRIP CP WINDRES NVCC BIN2C
SILENT = DEPCC DEPHOSTCC DEPAS DEPX86ASM RANLIB RM

MSG    = $@
M      = @$(call ECHO,$(TAG),$@);
$(foreach VAR,$(BRIEF), \
    $(eval override $(VAR) = @$$(call ECHO,$(VAR),$$(MSG)); $($(VAR))))
$(foreach VAR,$(SILENT),$(eval override $(VAR) = @$($(VAR))))
$(eval INSTALL = @$(call ECHO,INSTALL,$$(^:$(SRC_DIR)/%=%)); $(INSTALL))
endif

ALLFFLIBS = avcodec avdevice avfilter avformat avutil postproc swscale swresample

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
       $($(1)) $($(1)FLAGS) $($(2)) $($(1)_DEPFLAGS) $($(1)_C) $($(1)_O) $(patsubst $(SRC_PATH)/%,$(SRC_LINK)/%,$<)
endef

COMPILE_C = $(call COMPILE,CC)
COMPILE_CXX = $(call COMPILE,CXX)
COMPILE_S = $(call COMPILE,AS)
COMPILE_M = $(call COMPILE,OBJCC)
COMPILE_X86ASM = $(call COMPILE,X86ASM)
COMPILE_HOSTC = $(call COMPILE,HOSTCC)
COMPILE_NVCC = $(call COMPILE,NVCC)
COMPILE_MMI = $(call COMPILE,CC,MMIFLAGS)
COMPILE_MSA = $(call COMPILE,CC,MSAFLAGS)
COMPILE_LSX = $(call COMPILE,CC,LSXFLAGS)
COMPILE_LASX = $(call COMPILE,CC,LASXFLAGS)

%_mmi.o: %_mmi.c
	$(COMPILE_MMI)

%_msa.o: %_msa.c
	$(COMPILE_MSA)

%_lsx.o: %_lsx.c
	$(COMPILE_LSX)

%_lasx.o: %_lasx.c
	$(COMPILE_LASX)

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
	$(WINDRES) $(IFLAGS) $(foreach ARG,$(CC_DEPFLAGS),--preprocessor-arg "$(ARG)") -o $@ $<

%.i: %.c
	$(CC) $(CCFLAGS) $(CC_E) $<

%.h.c:
	$(Q)echo '#include "$*.h"' >$@

$(BIN2CEXE): ffbuild/bin2c_host.o
	$(HOSTLD) $(HOSTLDFLAGS) $(HOSTLD_O) $^ $(HOSTEXTRALIBS)

%.metal.air: %.metal
	$(METALCC) $< -o $@

%.metallib: %.metal.air
	$(METALLIB) --split-module-without-linking $< -o $@

%.metallib.c: %.metallib $(BIN2CEXE)
	$(BIN2C) $< $@ $(subst .,_,$(basename $(notdir $@)))

%.ptx: %.cu $(SRC_PATH)/compat/cuda/cuda_runtime.h
	$(COMPILE_NVCC)

ifdef CONFIG_PTX_COMPRESSION
%.ptx.gz: TAG = GZIP
%.ptx.gz: %.ptx
	$(M)gzip -c9 $(patsubst $(SRC_PATH)/%,$(SRC_LINK)/%,$<) >$@

%.ptx.c: %.ptx.gz $(BIN2CEXE)
	$(BIN2C) $(patsubst $(SRC_PATH)/%,$(SRC_LINK)/%,$<) $@ $(subst .,_,$(basename $(notdir $@)))
else
%.ptx.c: %.ptx $(BIN2CEXE)
	$(BIN2C) $(patsubst $(SRC_PATH)/%,$(SRC_LINK)/%,$<) $@ $(subst .,_,$(basename $(notdir $@)))
endif

clean::
	$(RM) $(BIN2CEXE)

%.c %.h %.pc %.ver %.version: TAG = GEN

# Dummy rule to stop make trying to rebuild removed or renamed headers
%.h %_template.c:
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
SHLIBOBJS += $(SHLIBOBJS-yes)
STLIBOBJS += $(STLIBOBJS-yes)
FFLIBS    := $($(NAME)_FFLIBS) $(FFLIBS-yes) $(FFLIBS)
TESTPROGS += $(TESTPROGS-yes)

LDLIBS       = $(FFLIBS:%=%$(BUILDSUF))
FFEXTRALIBS := $(LDLIBS:%=$(LD_LIB)) $(foreach lib,EXTRALIBS-$(NAME) $(FFLIBS:%=EXTRALIBS-%),$($(lib))) $(EXTRALIBS)

OBJS      := $(sort $(OBJS:%=$(SUBDIR)%))
SLIBOBJS  := $(sort $(SLIBOBJS:%=$(SUBDIR)%))
SHLIBOBJS := $(sort $(SHLIBOBJS:%=$(SUBDIR)%))
STLIBOBJS := $(sort $(STLIBOBJS:%=$(SUBDIR)%))
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
.SECONDARY:   $(HOBJS:.o=.c) $(PTXOBJS:.o=.c) $(PTXOBJS:.o=.gz) $(PTXOBJS:.o=)

alltools: $(TOOLS)

$(HOSTOBJS): %.o: %.c
	$(COMPILE_HOSTC)

$(HOSTPROGS): %$(HOSTEXESUF): %.o
	$(HOSTLD) $(HOSTLDFLAGS) $(HOSTLD_O) $^ $(HOSTEXTRALIBS)

$(OBJS):     | $(sort $(dir $(OBJS)))
$(HOBJS):    | $(sort $(dir $(HOBJS)))
$(HOSTOBJS): | $(sort $(dir $(HOSTOBJS)))
$(SLIBOBJS): | $(sort $(dir $(SLIBOBJS)))
$(SHLIBOBJS): | $(sort $(dir $(SHLIBOBJS)))
$(STLIBOBJS): | $(sort $(dir $(STLIBOBJS)))
$(TESTOBJS): | $(sort $(dir $(TESTOBJS)))
$(TOOLOBJS): | tools

OUTDIRS := $(OUTDIRS) $(dir $(OBJS) $(HOBJS) $(HOSTOBJS) $(SLIBOBJS) $(SHLIBOBJS) $(STLIBOBJS) $(TESTOBJS))

CLEANSUFFIXES     = *.d *.gcda *.gcno *.h.c *.ho *.map *.o *.pc *.ptx *.ptx.gz *.ptx.c *.ver *.version *$(DEFAULT_X86ASMD).asm *~ *.ilk *.pdb
LIBSUFFIXES       = *.a *.lib *.so *.so.* *.dylib *.dll *.def *.dll.a

define RULES
clean::
	$(RM) $(HOSTPROGS) $(TESTPROGS) $(TOOLS)
endef

$(eval $(RULES))

-include $(wildcard $(OBJS:.o=.d) $(HOSTOBJS:.o=.d) $(TESTOBJS:.o=.d) $(HOBJS:.o=.d) $(SHLIBOBJS:.o=.d) $(STLIBOBJS:.o=.d) $(SLIBOBJS:.o=.d)) $(OBJS:.o=$(DEFAULT_X86ASMD).d)
