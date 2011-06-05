#
# common bits used by all libraries
#

# first so "all" becomes default target
all: all-yes

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
