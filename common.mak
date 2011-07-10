#
# common bits used by all libraries
#

# first so "all" becomes default target
all: all-yes

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
