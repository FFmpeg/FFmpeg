
include ../config.mak

# Overload incdir, postproc include files go in a different directory.
incdir=$(prefix)/include/postproc

EXTRALIBS := -L$(BUILD_ROOT)/libavutil -lavutil$(BUILDSUF) $(EXTRALIBS)

NAME=postproc
LIBVERSION=$(SPPVERSION)
LIBMAJOR=$(SPPMAJOR)

STATIC_OBJS=postprocess.o
SHARED_OBJS=postprocess_pic.o

HEADERS = postprocess.h

include ../common.mak

depend dep: postprocess.c

postprocess_pic.o: postprocess.c
	$(CC) -c $(CFLAGS) -fomit-frame-pointer -fPIC -DPIC -o $@ $<

