#
# common bits used by all libraries
#

SRC_DIR = $(SRC_PATH)/lib$(NAME)
VPATH = $(SRC_DIR)

SRCS := $(OBJS:.o=.c) $(ASM_OBJS:.o=.S) $(CPPOBJS:.o=.cpp)
OBJS := $(OBJS) $(ASM_OBJS) $(CPPOBJS)
STATIC_OBJS := $(OBJS) $(STATIC_OBJS)
SHARED_OBJS := $(OBJS) $(SHARED_OBJS)

all: $(EXTRADEPS) $(LIB) $(SLIBNAME)

$(LIB): $(STATIC_OBJS)
	rm -f $@
	$(AR) rc $@ $^ $(EXTRAOBJS)
	$(RANLIB) $@

$(SLIBNAME): $(SLIBNAME_WITH_MAJOR)
	ln -sf $^ $@

$(SLIBNAME_WITH_MAJOR): $(SHARED_OBJS)
	$(CC) $(SHFLAGS) $(LDFLAGS) -o $@ $^ $(EXTRALIBS) $(EXTRAOBJS)
ifeq ($(CONFIG_WIN32),yes)
	-lib /machine:i386 /def:$(@:.dll=.def)
endif

%.o: %.c
	$(CC) $(CFLAGS) $(LIBOBJFLAGS) -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) $(LIBOBJFLAGS) -c -o $@ $<

# BeOS: remove -Wall to get rid of all the "multibyte constant" warnings
%.o: %.cpp
	g++ $(subst -Wall,,$(CFLAGS)) -c -o $@ $<

depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

dep:	depend

clean::
	rm -f *.o *.d *~ *.a *.lib *.so *.dylib *.dll \
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
	install -d "$(libdir)"
ifeq ($(CONFIG_WIN32),yes)
	install $(INSTALLSTRIP) -m 755 $(SLIBNAME) "$(prefix)"
else
	install $(INSTALLSTRIP) -m 755 $(SLIBNAME) \
		$(libdir)/$(SLIBNAME_WITH_VERSION)
	ln -sf $(SLIBNAME_WITH_VERSION) \
		$(libdir)/$(SLIBNAME_WITH_MAJOR)
	ln -sf $(SLIBNAME_WITH_VERSION) \
		$(libdir)/$(SLIBNAME)
endif

install-lib-static: $(LIB)
	install -d "$(libdir)"
	install -m 644 $(LIB) "$(libdir)"

install-headers:
	install -d "$(incdir)"
	install -d "$(libdir)/pkgconfig"
	install -m 644 $(addprefix "$(SRC_DIR)"/,$(HEADERS)) "$(incdir)"
	install -m 644 $(BUILD_ROOT)/lib$(NAME).pc "$(libdir)/pkgconfig"

uninstall: uninstall-libs uninstall-headers

uninstall-libs:
ifeq ($(CONFIG_WIN32),yes)
	-rm -f $(prefix)/$(SLIBNAME)
else
	-rm -f $(libdir)/$(SLIBNAME_WITH_MAJOR) \
	      $(libdir)/$(SLIBNAME)            \
	      $(libdir)/$(SLIBNAME_WITH_VERSION)
endif
	-rm -f $(libdir)/$(LIB)

uninstall-headers:
	rm -f $(addprefix $(incdir)/,$(HEADERS))
	rm -f $(libdir)/pkgconfig/lib$(NAME).pc

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
