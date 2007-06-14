include ../config.mak

VPATH=$(SRC_PATH_BARE)/vhook

CFLAGS=-I$(BUILD_ROOT) -I$(SRC_PATH) -I$(SRC_PATH)/libavutil -I$(SRC_PATH)/libavcodec \
       -I$(SRC_PATH)/libavformat -I$(SRC_PATH)/libswscale $(VHOOKCFLAGS) -DHAVE_AV_CONFIG_H
LDFLAGS+= -g

HOOKS=null$(SLIBSUF) fish$(SLIBSUF) watermark$(SLIBSUF)
ALLHOOKS=$(HOOKS) ppm$(SLIBSUF) imlib2$(SLIBSUF) drawtext$(SLIBSUF)

ifeq ($(HAVE_FORK),yes)
    HOOKS += ppm$(SLIBSUF)
endif

ifeq ($(HAVE_IMLIB2),yes)
    HOOKS += imlib2$(SLIBSUF)
    CFLAGS += `imlib2-config --cflags`
    LIBS_imlib2$(SLIBSUF) = `imlib2-config --libs`
endif

ifeq ($(HAVE_FREETYPE2),yes)
    HOOKS += drawtext$(SLIBSUF)
    CFLAGS += `freetype-config --cflags`
    LIBS_drawtext$(SLIBSUF) = `freetype-config --libs`
endif

SRCS := $(HOOKS:$(SLIBSUF)=.c)

all: $(HOOKS)

depend dep: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

install: $(HOOKS)
	install -d "$(shlibdir)/vhook"
	install -m 755 $(HOOKS) "$(shlibdir)/vhook"

uninstall:
	rm -f $(addprefix $(shlibdir)/vhook/,$(ALLHOOKS))
	-rmdir "$(shlibdir)/vhook/"

%$(SLIBSUF): %.o
	$(CC) $(LDFLAGS) -o $@ $(VHOOKSHFLAGS) $< $(VHOOKLIBS) $(LIBS_$@)

clean:
	rm -f *.o *~ *.a *.lib *.so *.dylib *.dll

distclean: clean
	rm -f .depend

.PHONY: all depend dep clean distclean install* uninstall*

-include .depend
