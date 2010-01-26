SRC_DIR := $(SRC_PATH_BARE)/lib$(NAME)

include $(SUBDIR)../common.mak

LIBVERSION := $(lib$(NAME)_VERSION)
LIBMAJOR   := $(lib$(NAME)_VERSION_MAJOR)

ifdef CONFIG_STATIC
all: $(SUBDIR)$(LIBNAME)

install-libs: install-lib$(NAME)-static

$(SUBDIR)$(LIBNAME): $(OBJS)
	rm -f $@
	$(AR) rc $@ $^ $(EXTRAOBJS)
	$(RANLIB) $@
endif

INCINSTDIR := $(INCDIR)/lib$(NAME)

THIS_LIB := $(SUBDIR)$($(CONFIG_SHARED:yes=S)LIBNAME)

define RULES
$(SUBDIR)%$(EXESUF): $(SUBDIR)%.o
	$(LD) $(FFLDFLAGS) -o $$@ $$^ -l$(FULLNAME) $(FFEXTRALIBS) $$(ELIBS)

$(SUBDIR)%-test.o: $(SUBDIR)%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -DTEST -c $$(CC_O) $$^

$(SUBDIR)%-test.o: $(SUBDIR)%-test.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -DTEST -c $$(CC_O) $$^

$(SUBDIR)x86/%.o: $(SUBDIR)x86/%.asm
	$(YASM) $(YASMFLAGS) -I $$(<D)/ -M -o $$@ $$< > $$(@:.o=.d)
	$(YASM) $(YASMFLAGS) -I $$(<D)/ -o $$@ $$<

clean::
	rm -f $(addprefix $(SUBDIR),*-example$(EXESUF) *-test$(EXESUF) $(CLEANFILES) $(CLEANSUFFIXES) $(LIBSUFFIXES)) \
	    $(addprefix $(SUBDIR), $(foreach suffix,$(CLEANSUFFIXES),$(addsuffix /$(suffix),$(DIRS)))) \
	    $(HOSTOBJS) $(HOSTPROGS)

distclean:: clean
	rm -f  $(addprefix $(SUBDIR),$(DISTCLEANSUFFIXES)) \
            $(addprefix $(SUBDIR), $(foreach suffix,$(DISTCLEANSUFFIXES),$(addsuffix /$(suffix),$(DIRS))))

ifdef CONFIG_SHARED
all: $(SUBDIR)$(SLIBNAME)

install-libs: install-lib$(NAME)-shared

$(SUBDIR)$(SLIBNAME): $(SUBDIR)$(SLIBNAME_WITH_MAJOR)
	cd ./$(SUBDIR) && $(LN_S) $(SLIBNAME_WITH_MAJOR) $(SLIBNAME)

$(SUBDIR)$(SLIBNAME_WITH_MAJOR): $(OBJS) $(SUBDIR)lib$(NAME).ver
	$(SLIB_CREATE_DEF_CMD)
	$(LD) $(SHFLAGS) $(FFLDFLAGS) -o $$@ $$(filter %.o,$$^) $(FFEXTRALIBS) $(EXTRAOBJS)
	$(SLIB_EXTRA_CMD)

ifdef SUBDIR
$(SUBDIR)$(SLIBNAME_WITH_MAJOR): $(DEP_LIBS)
endif
endif

install-lib$(NAME)-shared: $(SUBDIR)$(SLIBNAME)
	install -d "$(SHLIBDIR)"
	install -m 755 $$< "$(SHLIBDIR)/$(SLIBNAME_WITH_VERSION)"
	$(STRIP) "$(SHLIBDIR)/$(SLIBNAME_WITH_VERSION)"
	cd "$(SHLIBDIR)" && \
		$(LN_S) $(SLIBNAME_WITH_VERSION) $(SLIBNAME_WITH_MAJOR)
	cd "$(SHLIBDIR)" && \
		$(LN_S) $(SLIBNAME_WITH_VERSION) $(SLIBNAME)
	$(SLIB_INSTALL_EXTRA_CMD)

install-lib$(NAME)-static: $(SUBDIR)$(LIBNAME)
	install -d "$(LIBDIR)"
	install -m 644 $$< "$(LIBDIR)"
	$(LIB_INSTALL_EXTRA_CMD)

install-headers::
	install -d "$(INCINSTDIR)"
	install -d "$(LIBDIR)/pkgconfig"
	install -m 644 $(addprefix "$(SRC_DIR)"/,$(HEADERS)) "$(INCINSTDIR)"
ifdef BUILT_HEADERS
	install -m 644 $(addprefix $(SUBDIR),$(BUILT_HEADERS)) "$(INCINSTDIR)"
endif
	install -m 644 $(BUILD_ROOT)/lib$(NAME)/lib$(NAME).pc "$(LIBDIR)/pkgconfig"

uninstall-libs::
	-rm -f "$(SHLIBDIR)/$(SLIBNAME_WITH_MAJOR)" \
	       "$(SHLIBDIR)/$(SLIBNAME)"            \
	       "$(SHLIBDIR)/$(SLIBNAME_WITH_VERSION)"
	-$(SLIB_UNINSTALL_EXTRA_CMD)
	-rm -f "$(LIBDIR)/$(LIBNAME)"

uninstall-headers::
	rm -f $(addprefix "$(INCINSTDIR)/",$(HEADERS))
	rm -f "$(LIBDIR)/pkgconfig/lib$(NAME).pc"
	-rmdir "$(INCDIR)"
endef

$(eval $(RULES))

$(EXAMPLES) $(TESTPROGS): $(THIS_LIB) $(DEP_LIBS)

examples: $(EXAMPLES)
testprogs: $(TESTPROGS)
