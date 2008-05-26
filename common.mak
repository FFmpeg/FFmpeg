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
         -D_ISOC9X_SOURCE -I$(BUILD_ROOT) -I$(SRC_PATH) $(OPTFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(LIBOBJFLAGS) -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) $(LIBOBJFLAGS) -c -o $@ $<

%.ho: %.h
	$(CC) $(CFLAGS) $(LIBOBJFLAGS) -Wno-unused -c -o $@ -x c $<

%.d: %.c
	$(DEPEND_CMD) > $@

%.d: %.S
	$(DEPEND_CMD) > $@

%.d: %.cpp
	$(DEPEND_CMD) > $@

%$(EXESUF): %.c

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

DEP_LIBS:=$(foreach NAME,$(FFLIBS),lib$(NAME)/$($(BUILD_SHARED:yes=S)LIBNAME))

ALLHEADERS := $(subst $(SRC_DIR)/,$(SUBDIR),$(wildcard $(SRC_DIR)/*.h))
checkheaders: $(filter-out %_template.ho,$(ALLHEADERS:.h=.ho))

DEPS := $(OBJS:.o=.d)
depend dep: $(DEPS)

CLEANSUFFIXES = *.o *~ *.ho
LIBSUFFIXES   = *.a *.lib *.so *.so.* *.dylib *.dll *.def *.dll.a *.exp *.map
DISTCLEANSUFFIXES = *.d

define RULES
$(SUBDIR)%$(EXESUF): $(SUBDIR)%.o
	$(CC) $(FFLDFLAGS) -o $$@ $$^ $(SUBDIR)$(LIBNAME) $(FFEXTRALIBS)

$(SUBDIR)%-test.o: $(SUBDIR)%.c
	$(CC) $(CFLAGS) -DTEST -c -o $$@ $$^

$(SUBDIR)%-test.o: $(SUBDIR)%-test.c
	$(CC) $(CFLAGS) -DTEST -c -o $$@ $$^

clean::
	rm -f $(TESTS) $(addprefix $(SUBDIR),$(CLEANFILES) $(CLEANSUFFIXES) $(LIBSUFFIXES)) \
	    $(addprefix $(SUBDIR), $(foreach suffix,$(CLEANSUFFIXES),$(addsuffix /$(suffix),$(DIRS))))

distclean:: clean
	rm -f  $(addprefix $(SUBDIR),$(DISTCLEANSUFFIXES)) \
            $(addprefix $(SUBDIR), $(foreach suffix,$(DISTCLEANSUFFIXES),$(addsuffix /$(suffix),$(DIRS))))
endef

$(eval $(RULES))

tests: $(TESTS)

-include $(DEPS)
