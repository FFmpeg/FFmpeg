#
# common bits used by all libraries
#

VPATH = $(SRC_PATH_BARE)/lib$(NAME)
SRC_DIR = "$(VPATH)"

CFLAGS   += $(CFLAGS-yes)
OBJS     += $(OBJS-yes)
ASM_OBJS += $(ASM_OBJS-yes)
CPP_OBJS += $(CPP_OBJS-yes)

CFLAGS += -DHAVE_AV_CONFIG_H -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE \
          -D_ISOC9X_SOURCE -I$(BUILD_ROOT) -I$(SRC_PATH) \
          -I$(SRC_PATH)/libavutil $(OPTFLAGS)

SRCS := $(OBJS:.o=.c) $(ASM_OBJS:.o=.S) $(CPPOBJS:.o=.cpp)
OBJS := $(OBJS) $(ASM_OBJS) $(CPPOBJS)

all: $(LIBNAME) $(SLIBNAME)

$(LIBNAME): $(OBJS)
	rm -f $@
	$(AR) rc $@ $^ $(EXTRAOBJS)
	$(RANLIB) $@

$(SLIBNAME): $(SLIBNAME_WITH_MAJOR)
	$(LN_S) $^ $@

$(SLIBNAME_WITH_MAJOR): $(OBJS)
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

ALLHEADERS = $(subst $(VPATH)/,,$(wildcard $(VPATH)/*.h))
checkheaders: $(filter-out %_template.ho,$(ALLHEADERS:.h=.ho))

depend dep: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

clean::
	rm -f *.o *~ *.a *.lib *.so *.so.* *.dylib *.dll \
	      *.def *.dll.a *.exp *.ho

distclean: clean
	rm -f .depend

INSTALL_TARGETS-$(BUILD_SHARED) += install-lib-shared
INSTALL_TARGETS-$(BUILD_STATIC) += install-lib-static

install: install-libs install-headers

install-libs: $(INSTALL_TARGETS-yes)

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

install-headers:
	install -d "$(INCDIR)"
	install -d "$(LIBDIR)/pkgconfig"
	install -m 644 $(addprefix $(SRC_DIR)/,$(HEADERS)) "$(INCDIR)"
	install -m 644 $(BUILD_ROOT)/lib$(NAME).pc "$(LIBDIR)/pkgconfig"

uninstall: uninstall-libs uninstall-headers

uninstall-libs:
	-rm -f "$(SHLIBDIR)/$(SLIBNAME_WITH_MAJOR)" \
	       "$(SHLIBDIR)/$(SLIBNAME)"            \
	       "$(SHLIBDIR)/$(SLIBNAME_WITH_VERSION)"
	-$(SLIB_UNINSTALL_EXTRA_CMD)
	-rm -f "$(LIBDIR)/$(LIBNAME)"

uninstall-headers::
	rm -f $(addprefix "$(INCDIR)/",$(HEADERS))
	rm -f "$(LIBDIR)/pkgconfig/lib$(NAME).pc"

.PHONY: all depend dep clean distclean install* uninstall*

-include .depend
