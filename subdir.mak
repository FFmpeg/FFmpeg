SRC_DIR := $(SRC_PATH_BARE)/lib$(NAME)

include $(SUBDIR)../common.mak

LIBVERSION := $(lib$(NAME)_VERSION)
LIBMAJOR   := $(lib$(NAME)_VERSION_MAJOR)
INCINSTDIR := $(INCDIR)/lib$(NAME)
THIS_LIB   := $(SUBDIR)$($(CONFIG_SHARED:yes=S)LIBNAME)

all-$(CONFIG_STATIC): $(SUBDIR)$(LIBNAME)
all-$(CONFIG_SHARED): $(SUBDIR)$(SLIBNAME)

$(SUBDIR)%-test.o: $(SUBDIR)%-test.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -DTEST -c $(CC_O) $^

$(SUBDIR)%-test.o: $(SUBDIR)%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -DTEST -c $(CC_O) $^

$(SUBDIR)x86/%.o: $(SUBDIR)x86/%.asm
	$(YASMDEP) $(YASMFLAGS) -I $(<D)/ -M -o $@ $< > $(@:.o=.d)
	$(YASM) $(YASMFLAGS) -I $(<D)/ -o $@ $<

$(OBJS) $(SUBDIR)%.ho $(SUBDIR)%-test.o $(TESTOBJS): CPPFLAGS += -DHAVE_AV_CONFIG_H

$(SUBDIR)$(LIBNAME): $(OBJS)
	$(RM) $@
	$(AR) rc $@ $^ $(EXTRAOBJS)
	$(RANLIB) $@

install-headers: install-lib$(NAME)-headers install-lib$(NAME)-pkgconfig

install-libs-$(CONFIG_STATIC): install-lib$(NAME)-static
install-libs-$(CONFIG_SHARED): install-lib$(NAME)-shared

define RULES
$(SUBDIR)%$(EXESUF): $(SUBDIR)%.o
	$$(LD) $(FFLDFLAGS) -o $$@ $$^ -l$(FULLNAME) $(FFEXTRALIBS) $$(ELIBS)

$(SUBDIR)$(SLIBNAME): $(SUBDIR)$(SLIBNAME_WITH_MAJOR)
	$(Q)cd ./$(SUBDIR) && $(LN_S) $(SLIBNAME_WITH_MAJOR) $(SLIBNAME)

$(SUBDIR)$(SLIBNAME_WITH_MAJOR): $(OBJS) $(SUBDIR)lib$(NAME).ver
	$(SLIB_CREATE_DEF_CMD)
	$$(LD) $(SHFLAGS) $(FFLDFLAGS) -o $$@ $$(filter %.o,$$^) $(FFEXTRALIBS) $(EXTRAOBJS)
	$(SLIB_EXTRA_CMD)

ifdef SUBDIR
$(SUBDIR)$(SLIBNAME_WITH_MAJOR): $(DEP_LIBS)
endif

clean::
	$(RM) $(addprefix $(SUBDIR),*-example$(EXESUF) *-test$(EXESUF) $(CLEANFILES) $(CLEANSUFFIXES) $(LIBSUFFIXES)) \
	    $(foreach dir,$(DIRS),$(CLEANSUFFIXES:%=$(SUBDIR)$(dir)/%)) \
	    $(HOSTOBJS) $(HOSTPROGS)

distclean:: clean
	$(RM) $(DISTCLEANSUFFIXES:%=$(SUBDIR)%) \
	    $(foreach dir,$(DIRS),$(DISTCLEANSUFFIXES:%=$(SUBDIR)$(dir)/%))

install-lib$(NAME)-shared: $(SUBDIR)$(SLIBNAME)
	$(Q)mkdir -p "$(SHLIBDIR)"
	$$(INSTALL) -m 755 $$< "$(SHLIBDIR)/$(SLIBNAME_WITH_VERSION)"
	$$(STRIP) "$(SHLIBDIR)/$(SLIBNAME_WITH_VERSION)"
	$(Q)cd "$(SHLIBDIR)" && \
		$(LN_S) $(SLIBNAME_WITH_VERSION) $(SLIBNAME_WITH_MAJOR)
	$(Q)cd "$(SHLIBDIR)" && \
		$(LN_S) $(SLIBNAME_WITH_VERSION) $(SLIBNAME)
	$(SLIB_INSTALL_EXTRA_CMD)

install-lib$(NAME)-static: $(SUBDIR)$(LIBNAME)
	$(Q)mkdir -p "$(LIBDIR)"
	$$(INSTALL) -m 644 $$< "$(LIBDIR)"
	$(LIB_INSTALL_EXTRA_CMD)

install-lib$(NAME)-headers: $(addprefix $(SUBDIR),$(HEADERS) $(BUILT_HEADERS))
	$(Q)mkdir -p "$(INCINSTDIR)"
	$$(INSTALL) -m 644 $$^ "$(INCINSTDIR)"

install-lib$(NAME)-pkgconfig: $(SUBDIR)lib$(NAME).pc
	$(Q)mkdir -p "$(LIBDIR)/pkgconfig"
	$$(INSTALL) -m 644 $$^ "$(LIBDIR)/pkgconfig"

uninstall-libs::
	-$(RM) "$(SHLIBDIR)/$(SLIBNAME_WITH_MAJOR)" \
	       "$(SHLIBDIR)/$(SLIBNAME)"            \
	       "$(SHLIBDIR)/$(SLIBNAME_WITH_VERSION)"
	-$(SLIB_UNINSTALL_EXTRA_CMD)
	-$(RM) "$(LIBDIR)/$(LIBNAME)"

uninstall-headers::
	$(RM) $(addprefix "$(INCINSTDIR)/",$(HEADERS)) $(addprefix "$(INCINSTDIR)/",$(BUILT_HEADERS))
	$(RM) "$(LIBDIR)/pkgconfig/lib$(NAME).pc"
	-rmdir "$(INCINSTDIR)"
endef

$(eval $(RULES))

$(EXAMPLES) $(TESTPROGS): $(THIS_LIB) $(DEP_LIBS)

examples: $(EXAMPLES)
testprogs: $(TESTPROGS)
