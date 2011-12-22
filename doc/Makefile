MANPAGES    = $(PROGS-yes:%=doc/%.1)
PODPAGES    = $(PROGS-yes:%=doc/%.pod)
HTMLPAGES   = $(PROGS-yes:%=doc/%.html)                                 \
              doc/developer.html                                        \
              doc/faq.html                                              \
              doc/fate.html                                             \
              doc/general.html                                          \
              doc/git-howto.html                                        \
              doc/libavfilter.html                                      \
              doc/platform.html                                         \

TXTPAGES    = doc/fate.txt                                              \


DOCS = $(HTMLPAGES) $(MANPAGES) $(PODPAGES)
ifdef HAVE_MAKEINFO
DOCS += $(TXTPAGES)
endif

all-$(CONFIG_DOC): documentation

documentation: $(DOCS)

TEXIDEP = awk '/^@(verbatim)?include/ { printf "$@: $(@D)/%s\n", $$2 }' <$< >$(@:%=%.d)

doc/%.txt: TAG = TXT
doc/%.txt: doc/%.texi
	$(Q)$(TEXIDEP)
	$(M)makeinfo --force --no-headers -o $@ $< 2>/dev/null

doc/%.html: TAG = HTML
doc/%.html: doc/%.texi $(SRC_PATH)/doc/t2h.init
	$(Q)$(TEXIDEP)
	$(M)texi2html -monolithic --init-file $(SRC_PATH)/doc/t2h.init --output $@ $<

doc/%.pod: TAG = POD
doc/%.pod: doc/%.texi
	$(Q)$(TEXIDEP)
	$(M)$(SRC_PATH)/doc/texi2pod.pl $< $@

doc/%.1: TAG = MAN
doc/%.1: doc/%.pod
	$(M)pod2man --section=1 --center=" " --release=" " $< > $@

$(DOCS): | doc
OBJDIRS += doc

install-progs-$(CONFIG_DOC): install-man

install-man: $(MANPAGES)
	$(Q)mkdir -p "$(MANDIR)/man1"
	$(INSTALL) -m 644 $(MANPAGES) "$(MANDIR)/man1"

uninstall: uninstall-man

uninstall-man:
	$(RM) $(addprefix "$(MANDIR)/man1/",$(ALLMANPAGES))

clean::
	$(RM) $(TXTPAGES) doc/*.html doc/*.pod doc/*.1 $(CLEANSUFFIXES:%=doc/%)

-include $(wildcard $(DOCS:%=%.d))

.PHONY: documentation
