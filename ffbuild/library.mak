include $(SRC_PATH)/ffbuild/common.mak

ifeq (,$(filter %clean config,$(MAKECMDGOALS)))
-include $(SUBDIR)lib$(NAME).version
endif

LIBVERSION := $(lib$(NAME)_VERSION)
LIBMAJOR   := $(lib$(NAME)_VERSION_MAJOR)
LIBMINOR   := $(lib$(NAME)_VERSION_MINOR)
INCINSTDIR := $(INCDIR)/lib$(NAME)

INSTHEADERS := $(INSTHEADERS) $(HEADERS:%=$(SUBDIR)%)

all-$(CONFIG_STATIC): $(SUBDIR)$(LIBNAME)  $(SUBDIR)lib$(FULLNAME).pc
all-$(CONFIG_SHARED): $(SUBDIR)$(SLIBNAME) $(SUBDIR)lib$(FULLNAME).pc

LIBOBJS := $(OBJS) $(SHLIBOBJS) $(STLIBOBJS) $(SUBDIR)%.h.o $(TESTOBJS)
$(LIBOBJS) $(LIBOBJS:.o=.s) $(LIBOBJS:.o=.i):   CPPFLAGS += -DHAVE_AV_CONFIG_H

ifdef CONFIG_SHARED
# In case both shared libs and static libs are enabled, it can happen
# that a user might want to link e.g. libavformat statically, but
# libavcodec and the other libs dynamically. In this case
# libavformat won't be able to access libavcodec's internal symbols,
# so that they have to be duplicated into the archive just like
# for purely shared builds.
# Test programs are always statically linked against their library
# to be able to access their library's internals, even with shared builds.
# Yet linking against dependend libraries still uses dynamic linking.
# This means that we are in the scenario described above.
# In case only static libs are used, the linker will only use
# one of these copies; this depends on the duplicated object files
# containing exactly the same symbols.
OBJS += $(SHLIBOBJS)
endif
$(SUBDIR)$(LIBNAME): $(OBJS) $(STLIBOBJS)
	$(RM) $@
	$(AR) $(ARFLAGS) $(AR_O) $^
	$(RANLIB) $@

install-headers: install-lib$(NAME)-headers install-lib$(NAME)-pkgconfig

install-libs-$(CONFIG_STATIC): install-lib$(NAME)-static
install-libs-$(CONFIG_SHARED): install-lib$(NAME)-shared

define RULES
$(TOOLS):     THISLIB = $(FULLNAME:%=$(LD_LIB))
$(TESTPROGS): THISLIB = $(SUBDIR)$(LIBNAME)

$(LIBOBJS): CPPFLAGS += -DBUILDING_$(NAME)

$(TESTPROGS) $(TOOLS): %$(EXESUF): %.o
	$$(LD) $(LDFLAGS) $(LDEXEFLAGS) $$(LD_O) $$(filter %.o,$$^) $$(THISLIB) $(FFEXTRALIBS) $$(EXTRALIBS-$$(*F)) $$(ELIBS)

$(SUBDIR)lib$(NAME).version: $(SUBDIR)version.h | $(SUBDIR)
	$$(M) $$(SRC_PATH)/ffbuild/libversion.sh $(NAME) $$< > $$@

$(SUBDIR)lib$(FULLNAME).pc: $(SUBDIR)version.h ffbuild/config.sh | $(SUBDIR)
	$$(M) $$(SRC_PATH)/ffbuild/pkgconfig_generate.sh $(NAME) "$(DESC)"

$(SUBDIR)lib$(NAME).ver: $(SUBDIR)lib$(NAME).v $(OBJS)
	$$(M)sed 's/MAJOR/$(lib$(NAME)_VERSION_MAJOR)/' $$< | $(VERSION_SCRIPT_POSTPROCESS_CMD) > $$@

$(SUBDIR)$(SLIBNAME): $(SUBDIR)$(SLIBNAME_WITH_MAJOR)
	$(Q)cd ./$(SUBDIR) && $(LN_S) $(SLIBNAME_WITH_MAJOR) $(SLIBNAME)

$(SUBDIR)$(SLIBNAME_WITH_MAJOR): $(OBJS) $(SHLIBOBJS) $(SLIBOBJS) $(SUBDIR)lib$(NAME).ver
	$(SLIB_CREATE_DEF_CMD)
	$$(LD) $(SHFLAGS) $(LDFLAGS) $(LDSOFLAGS) $$(LD_O) $$(filter %.o,$$^) $(FFEXTRALIBS)
	$(SLIB_EXTRA_CMD)

ifdef SUBDIR
$(SUBDIR)$(SLIBNAME_WITH_MAJOR): $(DEP_LIBS)
endif

clean::
	$(RM) $(addprefix $(SUBDIR),$(CLEANFILES) $(CLEANSUFFIXES) $(LIBSUFFIXES)) \
	    $(CLEANSUFFIXES:%=$(SUBDIR)$(ARCH)/%) $(CLEANSUFFIXES:%=$(SUBDIR)tests/%)

install-lib$(NAME)-shared: $(SUBDIR)$(SLIBNAME)
	$(Q)mkdir -p "$(SHLIBDIR)"
	$$(INSTALL) -m 755 $$< "$(SHLIBDIR)/$(SLIB_INSTALL_NAME)"
	$$(STRIP) "$(SHLIBDIR)/$(SLIB_INSTALL_NAME)"
	$(Q)$(foreach F,$(SLIB_INSTALL_LINKS),(cd "$(SHLIBDIR)" && $(LN_S) $(SLIB_INSTALL_NAME) $(F));)
	$(if $(SLIB_INSTALL_EXTRA_SHLIB),$$(INSTALL) -m 644 $(SLIB_INSTALL_EXTRA_SHLIB:%=$(SUBDIR)%) "$(SHLIBDIR)")
	$(if $(SLIB_INSTALL_EXTRA_LIB),$(Q)mkdir -p "$(LIBDIR)")
	$(if $(SLIB_INSTALL_EXTRA_LIB),$$(INSTALL) -m 644 $(SLIB_INSTALL_EXTRA_LIB:%=$(SUBDIR)%) "$(LIBDIR)")

install-lib$(NAME)-static: $(SUBDIR)$(LIBNAME)
	$(Q)mkdir -p "$(LIBDIR)"
	$$(INSTALL) -m 644 $$< "$(LIBDIR)"
	$(LIB_INSTALL_EXTRA_CMD)

install-lib$(NAME)-headers: $(addprefix $(SUBDIR),$(HEADERS) $(BUILT_HEADERS))
	$(Q)mkdir -p "$(INCINSTDIR)"
	$$(INSTALL) -m 644 $$^ "$(INCINSTDIR)"

install-lib$(NAME)-pkgconfig: $(SUBDIR)lib$(FULLNAME).pc
	$(Q)mkdir -p "$(PKGCONFIGDIR)"
	$$(INSTALL) -m 644 $$^ "$(PKGCONFIGDIR)"

uninstall-libs::
	-$(RM) "$(SHLIBDIR)/$(SLIBNAME_WITH_MAJOR)" \
	       "$(SHLIBDIR)/$(SLIBNAME)"            \
	       "$(SHLIBDIR)/$(SLIBNAME_WITH_VERSION)"
	-$(RM)  $(SLIB_INSTALL_EXTRA_SHLIB:%="$(SHLIBDIR)/%")
	-$(RM)  $(SLIB_INSTALL_EXTRA_LIB:%="$(LIBDIR)/%")
	-$(RM) "$(LIBDIR)/$(LIBNAME)"

uninstall-headers::
	$(RM) $(addprefix "$(INCINSTDIR)/",$(HEADERS) $(BUILT_HEADERS))
	-rmdir "$(INCINSTDIR)"

uninstall-pkgconfig::
	$(RM) "$(PKGCONFIGDIR)/lib$(FULLNAME).pc"
endef

$(eval $(RULES))

$(TOOLS):     $(DEP_LIBS) $(SUBDIR)$($(CONFIG_SHARED:yes=S)LIBNAME)
$(TESTPROGS): $(DEP_LIBS) $(SUBDIR)$(LIBNAME)

testprogs: $(TESTPROGS)
