#
# common bits used by all libraries
#

all: # make "all" default target

ifndef SUBDIR
vpath %.c $(SRC_DIR)
vpath %.h $(SRC_DIR)
vpath %.S $(SRC_DIR)
vpath %.asm $(SRC_DIR)
vpath %.v   $(SRC_DIR)

ifeq ($(SRC_DIR),$(SRC_PATH_BARE))
BUILD_ROOT_REL = .
else
BUILD_ROOT_REL = ..
endif

ALLFFLIBS = avcodec avdevice avfilter avformat avutil postproc swscale

CFLAGS := -DHAVE_AV_CONFIG_H -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE \
          -I$(BUILD_ROOT_REL) -I$(SRC_PATH) $(OPTFLAGS)

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

%.o: %.d

%$(EXESUF): %.c

%.ver: %.v
	sed 's/$$MAJOR/$($(basename $(@F))_VERSION_MAJOR)/' $^ > $@

SVN_ENTRIES = $(SRC_PATH_BARE)/.svn/entries
ifeq ($(wildcard $(SVN_ENTRIES)),$(SVN_ENTRIES))
$(BUILD_ROOT_REL)/version.h: $(SVN_ENTRIES)
endif

$(BUILD_ROOT_REL)/version.h: $(SRC_PATH_BARE)/version.sh
	$< $(SRC_PATH) $@ $(EXTRA_VERSION)

install: install-libs install-headers

uninstall: uninstall-libs uninstall-headers

.PHONY: all depend dep clean distclean install* uninstall* tests
endif

CFLAGS   += $(CFLAGS-yes)
OBJS     += $(OBJS-yes)
FFLIBS   := $(FFLIBS-yes) $(FFLIBS)
TESTS    += $(TESTS-yes)

FFEXTRALIBS := $(addprefix -l,$(addsuffix $(BUILDSUF),$(FFLIBS))) $(EXTRALIBS)
FFLDFLAGS   := $(addprefix -L$(BUILD_ROOT)/lib,$(FFLIBS)) $(LDFLAGS)

OBJS  := $(addprefix $(SUBDIR),$(OBJS))
TESTS := $(addprefix $(SUBDIR),$(TESTS))

DEP_LIBS:=$(foreach NAME,$(FFLIBS),lib$(NAME)/$($(BUILD_SHARED:yes=S)LIBNAME))

ALLHEADERS := $(subst $(SRC_DIR)/,$(SUBDIR),$(wildcard $(SRC_DIR)/*.h $(SRC_DIR)/$(ARCH)/*.h))
checkheaders: $(filter-out %_template.ho,$(ALLHEADERS:.h=.ho))

DEPS := $(OBJS:.o=.d)
depend dep: $(DEPS)

CLEANSUFFIXES = *.o *~ *.ho *.ver
LIBSUFFIXES   = *.a *.lib *.so *.so.* *.dylib *.dll *.def *.dll.a *.exp *.map
DISTCLEANSUFFIXES = *.d *.pc

define RULES
$(SUBDIR)%$(EXESUF): $(SUBDIR)%.o
	$(CC) $(FFLDFLAGS) -o $$@ $$^ $(SUBDIR)$(LIBNAME) $(FFEXTRALIBS)

$(SUBDIR)%-test.o: $(SUBDIR)%.c
	$(CC) $(CFLAGS) -DTEST -c -o $$@ $$^

$(SUBDIR)%-test.o: $(SUBDIR)%-test.c
	$(CC) $(CFLAGS) -DTEST -c -o $$@ $$^

$(SUBDIR)x86/%.o: $(SUBDIR)x86/%.asm
	$(YASM) $(YASMFLAGS) -I $$(<D)/ -o $$@ $$<

$(SUBDIR)x86/%.d: $(SUBDIR)x86/%.asm
	$(YASM) $(YASMFLAGS) -I $$(<D)/ -M -o $$(@:%.d=%.o) $$< > $$@

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
