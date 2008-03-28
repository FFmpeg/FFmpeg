#
# common bits used by all libraries
#

SRC_DIR = $(SRC_PATH_BARE)/lib$(NAME)

LIBVERSION = $(lib$(NAME)_VERSION)
LIBMAJOR   = $(lib$(NAME)_VERSION_MAJOR)

vpath %.c $(SRC_DIR)
vpath %.h $(SRC_DIR)
vpath %.S $(SRC_DIR)

ALLFFLIBS = avcodec avdevice avfilter avformat avutil postproc swscale

CFLAGS   += $(CFLAGS-yes)
OBJS     += $(OBJS-yes)
ASM_OBJS += $(ASM_OBJS-yes)
CPP_OBJS += $(CPP_OBJS-yes)
FFLIBS   += $(FFLIBS-yes)

CFLAGS += -DHAVE_AV_CONFIG_H -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE \
          -D_ISOC9X_SOURCE -I$(BUILD_ROOT) -I$(SRC_PATH) \
          $(addprefix -I$(SRC_PATH)/lib,$(ALLFFLIBS)) $(OPTFLAGS)

EXTRALIBS := $(addprefix -l,$(addsuffix $(BUILDSUF),$(FFLIBS))) $(EXTRALIBS)
LDFLAGS   := $(addprefix -L$(BUILD_ROOT)/lib,$(FFLIBS)) $(LDFLAGS)

SRCS := $(OBJS:.o=.c) $(ASM_OBJS:.o=.S) $(CPP_OBJS:.o=.cpp)
OBJS := $(OBJS) $(ASM_OBJS) $(CPP_OBJS)

all: $(LIBNAME) $(SLIBNAME)

$(LIBNAME): $(OBJS)
	rm -f $@
	$(AR) rc $@ $^ $(EXTRAOBJS)
	$(RANLIB) $@

$(SLIBNAME): $(SLIBNAME_WITH_MAJOR)
	$(LN_S) $^ $@

$(SLIBNAME_WITH_MAJOR): $(OBJS)
	$(SLIB_CREATE_DEF_CMD)
	$(CC) $(SHFLAGS) $(LDFLAGS) -o $@ $^ $(EXTRALIBS) $(EXTRAOBJS)
	$(SLIB_EXTRA_CMD)

%.o: %.c
	$(CC) $(CFLAGS) $(LIBOBJFLAGS) -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) $(LIBOBJFLAGS) -c -o $@ $<

%: %.o $(LIBNAME)
	$(CC) $(LDFLAGS) -o $@ $^ $(EXTRALIBS)

%.ho: %.h
	$(CC) $(CFLAGS) $(LIBOBJFLAGS) -Wno-unused -c -o $@ -x c $<

ALLHEADERS = $(subst $(SRC_DIR)/,,$(wildcard $(SRC_DIR)/*.h))
checkheaders: $(filter-out %_template.ho,$(ALLHEADERS:.h=.ho))

depend dep: .depend

.depend: $(SRCS)
	$(DEPEND_CMD) > .depend

clean::
	rm -f *.o *~ *.a *.lib *.so *.so.* *.dylib *.dll \
	      *.def *.dll.a *.exp *.ho *.map $(TESTS)

distclean:: clean
	rm -f .depend

INSTALL_LIB_TARGETS-$(BUILD_SHARED) += install-lib-shared
INSTALL_LIB_TARGETS-$(BUILD_STATIC) += install-lib-static

install: install-libs install-headers

install-libs: $(INSTALL_LIB_TARGETS-yes)

install-lib-shared: $(SLIBNAME)
	install -d "$(SHLIBDIR)"
	install -m 755 $(SLIBNAME) "$(SHLIBDIR)/$(SLIBNAME_WITH_VERSION)"
	$(STRIP) "$(SHLIBDIR)/$(SLIBNAME_WITH_VERSION)"
	cd "$(SHLIBDIR)" && \
		$(LN_S) $(SLIBNAME_WITH_VERSION) $(SLIBNAME_WITH_MAJOR)
	cd "$(SHLIBDIR)" && \
		$(LN_S) $(SLIBNAME_WITH_VERSION) $(SLIBNAME)
	$(SLIB_INSTALL_EXTRA_CMD)

install-lib-static: $(LIBNAME)
	install -d "$(LIBDIR)"
	install -m 644 $(LIBNAME) "$(LIBDIR)"
	$(LIB_INSTALL_EXTRA_CMD)

INCINSTDIR = $(INCDIR)/lib$(NAME)

install-headers::
	install -d "$(INCINSTDIR)"
	install -d "$(LIBDIR)/pkgconfig"
	install -m 644 $(addprefix "$(SRC_DIR)"/,$(HEADERS)) "$(INCINSTDIR)"
	install -m 644 $(BUILD_ROOT)/lib$(NAME).pc "$(LIBDIR)/pkgconfig"

uninstall: uninstall-libs uninstall-headers

uninstall-libs::
	-rm -f "$(SHLIBDIR)/$(SLIBNAME_WITH_MAJOR)" \
	       "$(SHLIBDIR)/$(SLIBNAME)"            \
	       "$(SHLIBDIR)/$(SLIBNAME_WITH_VERSION)"
	-$(SLIB_UNINSTALL_EXTRA_CMD)
	-rm -f "$(LIBDIR)/$(LIBNAME)"

uninstall-headers::
	rm -f $(addprefix "$(INCINSTDIR)/",$(HEADERS))
	rm -f "$(LIBDIR)/pkgconfig/lib$(NAME).pc"

tests: $(TESTS)

%-test$(EXESUF): %.c $(LIBNAME)
	$(CC) $(CFLAGS) $(LDFLAGS) -DTEST -o $@ $^ $(EXTRALIBS)

.PHONY: all depend dep clean distclean install* uninstall* tests

-include .depend
