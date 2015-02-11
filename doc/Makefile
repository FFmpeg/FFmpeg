ALLMANPAGES = $(AVBASENAMES:%=%.1)
MANPAGES    = $(AVPROGS-yes:%=doc/%.1)
PODPAGES    = $(AVPROGS-yes:%=doc/%.pod)
HTMLPAGES   = $(AVPROGS-yes:%=doc/%.html)                               \
              doc/developer.html                                        \
              doc/faq.html                                              \
              doc/fate.html                                             \
              doc/general.html                                          \
              doc/git-howto.html                                        \
              doc/libavfilter.html                                      \
              doc/nut.html                                              \
              doc/platform.html                                         \

DOCS-$(CONFIG_POD2MAN)                          += $(MANPAGES) $(PODPAGES)
DOCS-$(CONFIG_TEXI2HTML)                        += $(HTMLPAGES)
DOCS = $(DOCS-yes)

DOC_EXAMPLES-$(CONFIG_AVCODEC_EXAMPLE)          += avcodec
DOC_EXAMPLES-$(CONFIG_FILTER_AUDIO_EXAMPLE)     += filter_audio
DOC_EXAMPLES-$(CONFIG_METADATA_EXAMPLE)         += metadata
DOC_EXAMPLES-$(CONFIG_OUTPUT_EXAMPLE)           += output
DOC_EXAMPLES-$(CONFIG_QSVDEC_EXAMPLE)           += qsvdec
DOC_EXAMPLES-$(CONFIG_TRANSCODE_AAC_EXAMPLE)    += transcode_aac
ALL_DOC_EXAMPLES = avcodec filter_audio metadata output transcode_aac

DOC_EXAMPLES     := $(DOC_EXAMPLES-yes:%=doc/examples/%$(EXESUF))
ALL_DOC_EXAMPLES := $(ALL_DOC_EXAMPLES:%=doc/examples/%$(EXESUF))
PROGS            += $(DOC_EXAMPLES)

all: $(DOCS)

apidoc: doc/doxy/html
documentation: $(DOCS)

examples: $(DOC_EXAMPLES)

TEXIDEP = awk '/^@include/ { printf "$@: $(@D)/%s\n", $$2 }' <$< >$(@:%=%.d)

GENTEXI  = format codec
GENTEXI := $(GENTEXI:%=doc/avoptions_%.texi)

$(GENTEXI): TAG = GENTEXI
$(GENTEXI): doc/avoptions_%.texi: doc/print_options$(HOSTEXESUF)
	$(M)doc/print_options $* > $@

doc/%.html: TAG = HTML
doc/%.html: doc/%.texi $(SRC_PATH)/doc/t2h.init $(GENTEXI)
	$(Q)$(TEXIDEP)
	$(M)texi2html -I doc -monolithic --init-file $(SRC_PATH)/doc/t2h.init --output $@ $<

doc/%.pod: TAG = POD
doc/%.pod: doc/%.texi $(SRC_PATH)/doc/texi2pod.pl $(GENTEXI)
	$(Q)$(TEXIDEP)
	$(M)$(SRC_PATH)/doc/texi2pod.pl -Idoc $< $@

doc/%.1: TAG = MAN
doc/%.1: doc/%.pod $(GENTEXI)
	$(M)pod2man --section=1 --center=" " --release=" " $< > $@

$(DOCS) doc/doxy/html: | doc/
$(DOC_EXAMPLES:%$(EXESUF)=%.o): | doc/examples
OBJDIRS += doc/examples

DOXY_INPUT      = $(addprefix $(SRC_PATH)/, $(INSTHEADERS) $(DOC_EXAMPLES:%$(EXESUF)=%.c) $(LIB_EXAMPLES:%$(EXESUF)=%.c))
DOXY_TEMPLATES  = doxy_stylesheet.css footer.html header.html
DOXY_TEMPLATES := $(addprefix $(SRC_PATH)/doc/doxy/, $(DOXY_TEMPLATES))

doc/doxy/html: $(SRC_PATH)/doc/Doxyfile $(DOXY_INPUT) $(DOXY_TEMPLATES)
	$(M)$(SRC_PATH)/doc/doxy-wrapper.sh $(SRC_PATH) $< $(DOXY_INPUT)

install-progs-$(CONFIG_POD2MAN): install-man
install-progs-$(CONFIG_TEXI2HTML): install-doc

install-doc: $(HTMLPAGES)
	$(Q)mkdir -p "$(DOCDIR)"
	$(INSTALL) -m 644 $(HTMLPAGES) "$(DOCDIR)"

install-man: $(MANPAGES)
	$(Q)mkdir -p "$(MANDIR)/man1"
	$(INSTALL) -m 644 $(MANPAGES) "$(MANDIR)/man1"

uninstall: uninstall-doc uninstall-man

uninstall-doc:
	$(RM) -r "$(DOCDIR)"

uninstall-man:
	$(RM) $(addprefix "$(MANDIR)/man1/",$(ALLMANPAGES))

clean::
	$(RM) $(ALL_DOC_EXAMPLES)
	$(RM) $(CLEANSUFFIXES:%=doc/%) $(CLEANSUFFIXES:%=doc/examples/%)
	$(RM) doc/*.html doc/*.pod doc/*.1 doc/avoptions_*.texi
	$(RM) -r doc/doxy/html

-include $(wildcard $(DOCS:%=%.d))

.PHONY: apidoc documentation
