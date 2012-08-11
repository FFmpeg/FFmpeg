MANPAGES    = $(PROGS-yes:%=doc/%.1)
PODPAGES    = $(PROGS-yes:%=doc/%.pod)
HTMLPAGES   = $(PROGS-yes:%=doc/%.html)                                 \
              doc/developer.html                                        \
              doc/faq.html                                              \
              doc/fate.html                                             \
              doc/general.html                                          \
              doc/git-howto.html                                        \
              doc/platform.html                                         \
              doc/syntax.html                                           \

TXTPAGES    = doc/fate.txt                                              \


DOCS-$(CONFIG_HTMLPAGES) += $(HTMLPAGES)
DOCS-$(CONFIG_PODPAGES)  += $(PODPAGES)
DOCS-$(CONFIG_MANPAGES)  += $(MANPAGES)
DOCS-$(CONFIG_TXTPAGES)  += $(TXTPAGES)
DOCS = $(DOCS-yes)

all-$(CONFIG_DOC): doc

doc: documentation

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
	$(M)texi2html -I doc -monolithic --init-file $(SRC_PATH)/doc/t2h.init --output $@ $<

doc/%.pod: TAG = POD
doc/%.pod: doc/%.texi $(GENTEXI)
	$(Q)$(TEXIDEP)
	$(M)perl $(SRC_PATH)/doc/texi2pod.pl -Idoc $< $@

doc/%.1: TAG = MAN
doc/%.1: doc/%.pod $(GENTEXI)
	$(M)pod2man --section=1 --center=" " --release=" " $< > $@

$(DOCS): | doc/

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

clean::
	$(RM) $(TXTPAGES) doc/*.html doc/*.pod doc/*.1 $(CLEANSUFFIXES:%=doc/%) doc/avoptions_*.texi

-include $(wildcard $(DOCS:%=%.d))

.PHONY: doc documentation
