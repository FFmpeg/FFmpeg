#
# common bits used by all libraries
#

all: # make "all" default target

ifndef SUBDIR
vpath %.c $(SRC_DIR)
vpath %.h $(SRC_DIR)
vpath %.S $(SRC_DIR)

ALLFFLIBS = avcodec avdevice avfilter avformat avutil postproc swscale

CFLAGS = -DHAVE_AV_CONFIG_H -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE \
         -D_ISOC9X_SOURCE -I$(BUILD_ROOT) -I$(SRC_PATH) \
         $(addprefix -I$(SRC_PATH)/lib,$(ALLFFLIBS)) $(OPTFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(LIBOBJFLAGS) -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) $(LIBOBJFLAGS) -c -o $@ $<

%.ho: %.h
	$(CC) $(CFLAGS) $(LIBOBJFLAGS) -Wno-unused -c -o $@ -x c $<

install: install-libs install-headers

uninstall: uninstall-libs uninstall-headers

.PHONY: all depend dep clean distclean install* uninstall* tests
endif

CFLAGS   += $(CFLAGS-yes)
OBJS     += $(OBJS-yes)
ASM_OBJS += $(ASM_OBJS-yes)
CPP_OBJS += $(CPP_OBJS-yes)
FFLIBS   := $(FFLIBS-yes) $(FFLIBS)
TESTS    += $(TESTS-yes)

FFEXTRALIBS := $(addprefix -l,$(addsuffix $(BUILDSUF),$(FFLIBS))) $(EXTRALIBS)
FFLDFLAGS   := $(addprefix -L$(BUILD_ROOT)/lib,$(FFLIBS)) $(LDFLAGS)

SRCS := $(OBJS:.o=.c) $(ASM_OBJS:.o=.S) $(CPP_OBJS:.o=.cpp)
OBJS := $(OBJS) $(ASM_OBJS) $(CPP_OBJS)

SRCS  := $(addprefix $(SUBDIR),$(SRCS))
OBJS  := $(addprefix $(SUBDIR),$(OBJS))
TESTS := $(addprefix $(SUBDIR),$(TESTS))

ALLHEADERS := $(subst $(SRC_DIR)/,$(SUBDIR),$(wildcard $(SRC_DIR)/*.h))
checkheaders: $(filter-out %_template.ho,$(ALLHEADERS:.h=.ho))

depend dep: $(SUBDIR).depend

CLEANFILES += *.o *~ *.a *.lib *.so *.so.* *.dylib *.dll \
              *.def *.dll.a *.exp *.ho *.map

define RULES
$(SUBDIR)%: $(SUBDIR)%.o $(LIBNAME)
	$(CC) $(FFLDFLAGS) -o $$@ $$^ $(FFEXTRALIBS)

$(SUBDIR)%-test$(EXESUF): $(SUBDIR)%.c $(LIBNAME)
	$(CC) $(CFLAGS) $(FFLDFLAGS) -DTEST -o $$@ $$^ $(FFEXTRALIBS)

$(SUBDIR).depend: $(SRCS)
	$(DEPEND_CMD) > $$@

clean::
	rm -f $(TESTS) $(addprefix $(SUBDIR),$(CLEANFILES))

distclean:: clean
	rm -f $(SUBDIR).depend
endef

$(eval $(RULES))

tests: $(TESTS)

-include $(SUBDIR).depend
