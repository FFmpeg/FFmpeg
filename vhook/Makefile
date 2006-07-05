include ../config.mak

VPATH=$(SRC_PATH)/vhook

CFLAGS=-fPIC $(SHCFLAGS) -I.. -I$(SRC_PATH) -I$(SRC_PATH)/libavformat \
       -I$(SRC_PATH)/libavcodec -I$(SRC_PATH)/libavutil -DHAVE_AV_CONFIG_H

#FIXME: This needs to be in configure/config.mak
ifeq ($(CONFIG_DARWIN),yes)
	SHFLAGS = -dynamiclib -Wl,-single_module -flat_namespace \
                  -undefined suppress -Wl,-install_name,$(libdir)/vhook/$@
endif
HOOKS=null$(SLIBSUF) fish$(SLIBSUF) ppm$(SLIBSUF) watermark$(SLIBSUF)

ifeq ($(HAVE_IMLIB2),yes)
    HOOKS += imlib2$(SLIBSUF)
    LDFLAGS += -lImlib2
endif

ifeq ($(HAVE_FREETYPE2),yes)
    HOOKS += drawtext$(SLIBSUF)
    CFLAGS += `freetype-config --cflags`
    LDFLAGS += `freetype-config --libs`
endif

all: $(HOOKS)

SRCS := $(HOOKS:$(SLIBSUF)=.c)

depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

install: $(HOOKS)
	install -d "$(libdir)/vhook"
	install -m 755 $(HOOKS) "$(libdir)/vhook"

ALLHOOKS=$(HOOKS) imlib2$(SLIBSUF) drawtext$(SLIBSUF)
uninstall:
	rm -f $(addprefix $(libdir)/vhook/,$(ALLHOOKS))
	-rmdir "$(libdir)/vhook/"

%$(SLIBSUF): %.o
	$(CC) $(LDFLAGS) -g -o $@ $(SHFLAGS) $<

clean:
	rm -f *.o *.d *~ *.a *.lib *.so *.dylib *.dll

distclean: clean
	rm -f .depend

ifneq ($(wildcard .depend),)
include .depend
endif
