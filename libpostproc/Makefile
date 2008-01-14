
include ../config.mak

# Overload incdir, postproc include files go in a different directory.
INCDIR=$(prefix)/include/postproc

EXTRALIBS := -L$(BUILD_ROOT)/libavutil -lavutil$(BUILDSUF) $(EXTRALIBS)

NAME=postproc
LIBVERSION=$(SPPVERSION)
LIBMAJOR=$(SPPMAJOR)

HEADERS = postprocess.h

OBJS = postprocess.o

include ../common.mak

uninstall-headers::
	-rmdir "$(prefix)/include/postproc"
