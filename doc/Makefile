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

MANPAGES1   = $(AVPROGS-yes:%=doc/%.1)    $(AVPROGS-yes:%=doc/%-all.1)    $(COMPONENTS-yes:%=doc/%.1)
MANPAGES3   = $(LIBRARIES-yes:%=doc/%.3)
MANPAGES    = $(MANPAGES1) $(MANPAGES3)
PODPAGES    = $(AVPROGS-yes:%=doc/%.pod)  $(AVPROGS-yes:%=doc/%-all.pod)  $(COMPONENTS-yes:%=doc/%.pod)  $(LIBRARIES-yes:%=doc/%.pod)
HTMLPAGES   = $(AVPROGS-yes:%=doc/%.html) $(AVPROGS-yes:%=doc/%-all.html) $(COMPONENTS-yes:%=doc/%.html) $(LIBRARIES-yes:%=doc/%.html) \
              doc/community.html                                        \
              doc/developer.html                                        \
              doc/faq.html                                              \
              doc/fate.html                                             \
              doc/general.html                                          \
              doc/git-howto.html                                        \
              doc/mailing-list-faq.html                                 \
              doc/nut.html                                              \
              doc/platform.html                                         \
              $(SRC_PATH)/doc/bootstrap.min.css                         \
              $(SRC_PATH)/doc/style.min.css                             \
              $(SRC_PATH)/doc/default.css                               \

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

TEXIDEP = perl $(SRC_PATH)/doc/texidep.pl $(SRC_PATH) $< $@ >$(@:%=%.d)

doc/%.txt: TAG = TXT
doc/%.txt: doc/%.texi
	$(Q)$(TEXIDEP)
	$(M)makeinfo --force --no-headers -o $@ $< 2>/dev/null

GENTEXI  = format codec
GENTEXI := $(GENTEXI:%=doc/avoptions_%.texi)

$(GENTEXI): TAG = GENTEXI
$(GENTEXI): doc/avoptions_%.texi: doc/print_options$(HOSTEXESUF)
	$(M)doc/print_options$(HOSTEXESUF) $* > $@

doc/%.html: TAG = HTML
doc/%-all.html: TAG = HTML

ifdef HAVE_MAKEINFO_HTML
doc/%.html: doc/%.texi $(SRC_PATH)/doc/t2h.pm $(GENTEXI)
	$(Q)$(TEXIDEP)
	$(M)makeinfo --html -I doc --no-split -D config-not-all --init-file=$(SRC_PATH)/doc/t2h.pm --output $@ $<

doc/%-all.html: doc/%.texi $(SRC_PATH)/doc/t2h.pm $(GENTEXI)
	$(Q)$(TEXIDEP)
	$(M)makeinfo --html -I doc --no-split -D config-all --init-file=$(SRC_PATH)/doc/t2h.pm --output $@ $<
else
doc/%.html: doc/%.texi $(SRC_PATH)/doc/t2h.init $(GENTEXI)
	$(Q)$(TEXIDEP)
	$(M)texi2html -I doc -monolithic --D=config-not-all --init-file $(SRC_PATH)/doc/t2h.init --output $@ $<

doc/%-all.html: doc/%.texi $(SRC_PATH)/doc/t2h.init $(GENTEXI)
	$(Q)$(TEXIDEP)
	$(M)texi2html -I doc -monolithic --D=config-all --init-file $(SRC_PATH)/doc/t2h.init --output $@ $<
endif

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
	$(M)pod2man --section=1 --center=" " --release=" " --date=" " $< > $@
doc/%.3: doc/%.pod $(GENTEXI)
	$(M)pod2man --section=3 --center=" " --release=" " --date=" " $< > $@

$(DOCS) doc/doxy/html: | doc/

DOXY_INPUT      = $(INSTHEADERS)
DOXY_INPUT_DEPS = $(addprefix $(SRC_PATH)/, $(DOXY_INPUT)) ffbuild/config.mak

doc/doxy/html: TAG = DOXY
doc/doxy/html: $(SRC_PATH)/doc/Doxyfile $(SRC_PATH)/doc/doxy-wrapper.sh $(DOXY_INPUT_DEPS)
	$(M)$(SRC_PATH)/doc/doxy-wrapper.sh $$PWD/doc/doxy $(SRC_PATH) doc/Doxyfile $(DOXYGEN) $(DOXY_INPUT);

install-doc: install-html install-man

install-html:

install-man:

ifdef CONFIG_HTMLPAGES
install-progs-$(CONFIG_DOC): install-html

install-html: $(HTMLPAGES)
	$(Q)mkdir -p "$(DOCDIR)"
	$(INSTALL) -m 644 $(HTMLPAGES) "$(DOCDIR)"
endif

ifdef CONFIG_MANPAGES
install-progs-$(CONFIG_DOC): install-man

install-man: $(MANPAGES)
	$(Q)mkdir -p "$(MANDIR)/man1"
	$(INSTALL) -m 644 $(MANPAGES1) "$(MANDIR)/man1"
	$(Q)mkdir -p "$(MANDIR)/man3"
	$(INSTALL) -m 644 $(MANPAGES3) "$(MANDIR)/man3"
endif

uninstall: uninstall-doc

uninstall-doc: uninstall-html uninstall-man

uninstall-html:
	$(RM) -r "$(DOCDIR)"

uninstall-man:
	$(RM) $(addprefix "$(MANDIR)/man1/",$(AVPROGS-yes:%=%.1) $(AVPROGS-yes:%=%-all.1) $(COMPONENTS-yes:%=%.1))
	$(RM) $(addprefix "$(MANDIR)/man3/",$(LIBRARIES-yes:%=%.3))

clean:: docclean

distclean:: docclean
	$(RM) doc/config.texi

docclean::
	$(RM) $(CLEANSUFFIXES:%=doc/%)
	$(RM) $(TXTPAGES) doc/*.html doc/*.pod doc/*.1 doc/*.3 doc/avoptions_*.texi
	$(RM) -r doc/doxy/html

-include $(wildcard $(DOCS:%=%.d))

.PHONY: apidoc doc documentation
