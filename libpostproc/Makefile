
include ../config.mak

# Overload incdir, postproc include files go in a different directory.
INCDIR=$(prefix)/include/postproc

EXTRALIBS := -L$(BUILD_ROOT)/libavutil -lavutil$(BUILDSUF) $(EXTRALIBS)

NAME=postproc
LIBVERSION=$(SPPVERSION)
LIBMAJOR=$(SPPMAJOR)

HEADERS = postprocess.h

include ../common.mak

$(LIBNAME): postprocess.o
$(SLIBNAME_WITH_MAJOR): postprocess_pic.o

depend dep: postprocess.c

postprocess_pic.o: postprocess.c
	$(CC) -c $(CFLAGS) -fomit-frame-pointer -fPIC -DPIC -o $@ $<

uninstall-headers::
	-rmdir "$(prefix)/include/postproc"
