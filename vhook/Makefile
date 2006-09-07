include ../config.mak

VPATH=$(SRC_PATH)/vhook

CFLAGS=-I.. -I$(SRC_PATH) -I$(SRC_PATH)/libavutil -I$(SRC_PATH)/libavcodec \
       -I$(SRC_PATH)/libavformat $(VHOOKCFLAGS) -DHAVE_AV_CONFIG_H

HOOKS=null$(SLIBSUF) fish$(SLIBSUF) ppm$(SLIBSUF) watermark$(SLIBSUF)
ALLHOOKS=$(HOOKS) imlib2$(SLIBSUF) drawtext$(SLIBSUF)

ifeq ($(HAVE_IMLIB2),yes)
    HOOKS += imlib2$(SLIBSUF)
    LIBS_imlib2$(SLIBSUF) = -lImlib2
endif

ifeq ($(HAVE_FREETYPE2),yes)
    HOOKS += drawtext$(SLIBSUF)
    CFLAGS += `freetype-config --cflags`
    LIBS_drawtext$(SLIBSUF) = `freetype-config --libs`
endif

SRCS := $(HOOKS:$(SLIBSUF)=.c)

all: $(HOOKS)

depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

install: $(HOOKS)
	install -d "$(shlibdir)/vhook"
	install -m 755 $(HOOKS) "$(shlibdir)/vhook"

uninstall:
	rm -f $(addprefix $(shlibdir)/vhook/,$(ALLHOOKS))
	-rmdir "$(shlibdir)/vhook/"

%$(SLIBSUF): %.o
	$(CC) $(LDFLAGS) -g -o $@ $(VHOOKSHFLAGS) $< $(LIBS_$@)

clean:
	rm -f *.o *.d *~ *.a *.lib *.so *.dylib *.dll

distclean: clean
	rm -f .depend

ifneq ($(wildcard .depend),)
include .depend
endif
