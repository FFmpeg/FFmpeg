SRC_DIR := $(SRC_PATH_BARE)/lib$(NAME)

include $(SUBDIR)../common.mak

LIBVERSION := $(lib$(NAME)_VERSION)
LIBMAJOR   := $(lib$(NAME)_VERSION_MAJOR)

ifeq ($(BUILD_STATIC),yes)
all: $(SUBDIR)$(LIBNAME)

install-libs: install-lib$(NAME)-static

$(SUBDIR)$(LIBNAME): $(OBJS)
	rm -f $@
	$(AR) rc $@ $^ $(EXTRAOBJS)
	$(RANLIB) $@
endif

INCINSTDIR := $(INCDIR)/lib$(NAME)

define RULES
ifdef BUILD_SHARED
all: $(SUBDIR)$(SLIBNAME)

install-libs: install-lib$(NAME)-shared

$(SUBDIR)$(SLIBNAME): $(SUBDIR)$(SLIBNAME_WITH_MAJOR)
	cd ./$(SUBDIR) && $(LN_S) $(SLIBNAME_WITH_MAJOR) $(SLIBNAME)

$(SUBDIR)$(SLIBNAME_WITH_MAJOR): $(OBJS) $(SUBDIR)lib$(NAME).ver
	$(SLIB_CREATE_DEF_CMD)
	$(CC) $(SHFLAGS) $(FFLDFLAGS) -o $$@ $$(filter-out $(SUBDIR)lib$(NAME).ver $(DEP_LIBS),$$^) $(FFEXTRALIBS) $(EXTRAOBJS)
	$(SLIB_EXTRA_CMD)

ifdef SUBDIR
$(SUBDIR)$(SLIBNAME_WITH_MAJOR): $(DEP_LIBS)
endif
endif

install-lib$(NAME)-shared: $(SUBDIR)$(SLIBNAME)
	install -d "$(SHLIBDIR)"
	install -m 755 $(SUBDIR)$(SLIBNAME) "$(SHLIBDIR)/$(SLIBNAME_WITH_VERSION)"
	$(STRIP) "$(SHLIBDIR)/$(SLIBNAME_WITH_VERSION)"
	cd "$(SHLIBDIR)" && \
		$(LN_S) $(SLIBNAME_WITH_VERSION) $(SLIBNAME_WITH_MAJOR)
	cd "$(SHLIBDIR)" && \
		$(LN_S) $(SLIBNAME_WITH_VERSION) $(SLIBNAME)
	$(SLIB_INSTALL_EXTRA_CMD)

install-lib$(NAME)-static: $(SUBDIR)$(LIBNAME)
	install -d "$(LIBDIR)"
	install -m 644 $(SUBDIR)$(LIBNAME) "$(LIBDIR)"
	$(LIB_INSTALL_EXTRA_CMD)

install-headers::
	install -d "$(INCINSTDIR)"
	install -d "$(LIBDIR)/pkgconfig"
	install -m 644 $(addprefix "$(SRC_DIR)"/,$(HEADERS)) "$(INCINSTDIR)"
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
