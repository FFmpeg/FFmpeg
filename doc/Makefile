LIBRARIES-$(CONFIG_AVUTIL)     += libavutil
LIBRARIES-$(CONFIG_SWSCALE)    += libswscale
LIBRARIES-$(CONFIG_SWRESAMPLE) += libswresample
LIBRARIES-$(CONFIG_AVCODEC)    += libavcodec
LIBRARIES-$(CONFIG_AVFORMAT)   += libavformat
LIBRARIES-$(CONFIG_AVDEVICE)   += libavdevice
LIBRARIES-$(CONFIG_AVFILTER)   += libavfilter

COMPONENTS-$(CONFIG_AVUTIL)     += ffmpeg-utils
COMPONENTS-$(CONFIG_SWSCALE)    += ffmpeg-scaler
COMPONENTS-$(CONFIG_SWRESAMPLE) += ffmpeg-resampler
COMPONENTS-$(CONFIG_AVCODEC)    += ffmpeg-codecs ffmpeg-bitstream-filters
COMPONENTS-$(CONFIG_AVFORMAT)   += ffmpeg-formats ffmpeg-protocols
COMPONENTS-$(CONFIG_AVDEVICE)   += ffmpeg-devices
COMPONENTS-$(CONFIG_AVFILTER)   += ffmpeg-filters

MANPAGES    = $(PROGS-yes:%=doc/%.1)    $(PROGS-yes:%=doc/%-all.1)    $(COMPONENTS-yes:%=doc/%.1)    $(LIBRARIES-yes:%=doc/%.3)
PODPAGES    = $(PROGS-yes:%=doc/%.pod)  $(PROGS-yes:%=doc/%-all.pod)  $(COMPONENTS-yes:%=doc/%.pod)  $(LIBRARIES-yes:%=doc/%.pod)
HTMLPAGES   = $(PROGS-yes:%=doc/%.html) $(PROGS-yes:%=doc/%-all.html) $(COMPONENTS-yes:%=doc/%.html) $(LIBRARIES-yes:%=doc/%.html) \
              doc/developer.html                                        \
              doc/faq.html                                              \
              doc/fate.html                                             \
              doc/general.html                                          \
              doc/git-howto.html                                        \
              doc/nut.html                                              \
              doc/platform.html                                         \

TXTPAGES    = doc/fate.txt                                              \


DOCS-$(CONFIG_HTMLPAGES) += $(HTMLPAGES)
DOCS-$(CONFIG_PODPAGES)  += $(PODPAGES)
DOCS-$(CONFIG_MANPAGES)  += $(MANPAGES)
DOCS-$(CONFIG_TXTPAGES)  += $(TXTPAGES)
DOCS = $(DOCS-yes)

all-$(CONFIG_DOC): doc

doc: documentation

apidoc: doc/doxy/html
documentation: $(DOCS)

TEXIDEP = awk '/^@(verbatim)?include/ { printf "$@: $(@D)/%s\n", $$2 }' <$< >$(@:%=%.d)

doc/%.txt: TAG = TXT
doc/%.txt: doc/%.texi
	$(Q)$(TEXIDEP)
	$(M)makeinfo --force --no-headers -o $@ $< 2>/dev/null

GENTEXI  = format codec
GENTEXI := $(GENTEXI:%=doc/avoptions_%.texi)

$(GENTEXI): TAG = GENTEXI
$(GENTEXI): doc/avoptions_%.texi: doc/print_options$(HOSTEXESUF)
	$(M)doc/print_options $* > $@

doc/%.html: TAG = HTML
doc/%.html: doc/%.texi $(SRC_PATH)/doc/t2h.init $(GENTEXI)
	$(Q)$(TEXIDEP)
	$(M)texi2html -I doc -monolithic --D=config-not-all --init-file $(SRC_PATH)/doc/t2h.init --output $@ $<

doc/%-all.html: TAG = HTML
doc/%-all.html: doc/%.texi $(SRC_PATH)/doc/t2h.init $(GENTEXI)
	$(Q)$(TEXIDEP)
	$(M)texi2html -I doc -monolithic --D=config-all --init-file $(SRC_PATH)/doc/t2h.init --output $@ $<

doc/%.pod: TAG = POD
doc/%.pod: doc/%.texi $(SRC_PATH)/doc/texi2pod.pl $(GENTEXI)
	$(Q)$(TEXIDEP)
	$(M)perl $(SRC_PATH)/doc/texi2pod.pl -Dconfig-not-all=yes -Idoc $< $@

doc/%-all.pod: TAG = POD
doc/%-all.pod: doc/%.texi $(SRC_PATH)/doc/texi2pod.pl $(GENTEXI)
	$(Q)$(TEXIDEP)
	$(M)perl $(SRC_PATH)/doc/texi2pod.pl -Dconfig-all=yes -Idoc $< $@

doc/%.1 doc/%.3: TAG = MAN
doc/%.1: doc/%.pod $(GENTEXI)
	$(M)pod2man --section=1 --center=" " --release=" " $< > $@
doc/%.3: doc/%.pod $(GENTEXI)
	$(M)pod2man --section=3 --center=" " --release=" " $< > $@

$(DOCS) doc/doxy/html: | doc/

doc/doxy/html: $(SRC_PATH)/doc/Doxyfile $(INSTHEADERS)
	$(M)$(SRC_PATH)/doc/doxy-wrapper.sh $(SRC_PATH) $^

install-man:

ifdef CONFIG_MANPAGES
install-progs-$(CONFIG_DOC): install-man

install-man: $(MANPAGES)
	$(Q)mkdir -p "$(MANDIR)/man1"
	$(INSTALL) -m 644 $(MANPAGES) "$(MANDIR)/man1"
endif

uninstall: uninstall-man

uninstall-man:
	$(RM) $(addprefix "$(MANDIR)/man1/",$(ALLMANPAGES))

clean:: docclean

distclean:: docclean
	$(RM) doc/config.texi

docclean:
	$(RM) $(TXTPAGES) doc/*.html doc/*.pod doc/*.1 doc/*.3 $(CLEANSUFFIXES:%=doc/%) doc/avoptions_*.texi
	$(RM) -r doc/doxy/html

-include $(wildcard $(DOCS:%=%.d))

.PHONY: apidoc doc documentation
