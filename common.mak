#
# common bits used by all libraries
#

SRC_DIR = $(SRC_PATH)/lib$(NAME)
VPATH = $(SRC_DIR)

SRCS := $(OBJS:.o=.c) $(ASM_OBJS:.o=.S) $(CPPOBJS:.o=.cpp)
OBJS := $(OBJS) $(ASM_OBJS) $(CPPOBJS)
STATIC_OBJS := $(OBJS) $(STATIC_OBJS)
SHARED_OBJS := $(OBJS) $(SHARED_OBJS)

EXTRALIBS := -L$(BUILD_ROOT)/libavutil -lavutil$(BUILDSUF) $(EXTRALIBS)

all: $(EXTRADEPS) $(LIB) $(SLIBNAME)

$(LIB): $(STATIC_OBJS)
	rm -f $@
	$(AR) rc $@ $^ $(EXTRAOBJS)
	$(RANLIB) $@

$(SLIBNAME): $(SLIBNAME_WITH_MAJOR)
	ln -sf $^ $@

$(SLIBNAME_WITH_MAJOR): $(SHARED_OBJS)
	$(CC) $(SHFLAGS) $(LDFLAGS) -o $@ $^ $(EXTRALIBS) $(EXTRAOBJS)
	$(SLIB_EXTRA_CMD)

%.o: %.c
	$(CC) $(CFLAGS) $(LIBOBJFLAGS) -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) $(LIBOBJFLAGS) -c -o $@ $<

# BeOS: remove -Wall to get rid of all the "multibyte constant" warnings
%.o: %.cpp
	g++ $(subst -Wall,,$(CFLAGS)) -c -o $@ $<

%: %.o $(LIB)
	$(CC) $(LDFLAGS) -o $@ $^ $(EXTRALIBS)

depend dep: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

clean::
	rm -f *.o *.d *~ *.a *.lib *.so *.so.* *.dylib *.dll \
	      *.lib *.def *.dll.a *.exp

distclean: clean
	rm -f .depend

ifeq ($(BUILD_SHARED),yes)
INSTLIBTARGETS += install-lib-shared
endif
ifeq ($(BUILD_STATIC),yes)
INSTLIBTARGETS += install-lib-static
endif

install: install-libs install-headers

install-libs: $(INSTLIBTARGETS)

install-lib-shared: $(SLIBNAME)
	install -d "$(shlibdir)"
	install $(INSTALLSTRIP) -m 755 $(SLIBNAME) \
		"$(shlibdir)/$(SLIBNAME_WITH_VERSION)"
	cd "$(shlibdir)" && \
		ln -sf $(SLIBNAME_WITH_VERSION) $(SLIBNAME_WITH_MAJOR)
	cd "$(shlibdir)" && \
		ln -sf $(SLIBNAME_WITH_VERSION) $(SLIBNAME)

install-lib-static: $(LIB)
	install -d "$(libdir)"
	install -m 644 $(LIB) "$(libdir)"
	$(LIB_INSTALL_EXTRA_CMD)

install-headers:
	install -d "$(incdir)"
	install -d "$(libdir)/pkgconfig"
	install -m 644 $(addprefix "$(SRC_DIR)"/,$(HEADERS)) "$(incdir)"
	install -m 644 $(BUILD_ROOT)/lib$(NAME).pc "$(libdir)/pkgconfig"

uninstall: uninstall-libs uninstall-headers

uninstall-libs:
	-rm -f "$(shlibdir)/$(SLIBNAME_WITH_MAJOR)" \
	       "$(shlibdir)/$(SLIBNAME)"            \
	       "$(shlibdir)/$(SLIBNAME_WITH_VERSION)"
	-rm -f "$(libdir)/$(LIB)"

uninstall-headers:
	rm -f $(addprefix "$(incdir)/",$(HEADERS))
	rm -f "$(libdir)/pkgconfig/lib$(NAME).pc"

.PHONY: all depend dep clean distclean install* uninstall*

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
