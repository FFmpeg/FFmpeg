TOOLS = qt-faststart trasher
TOOLS-$(CONFIG_ZLIB) += cws2fws

tools/cws2fws$(EXESUF): ELIBS = $(ZLIB)

OBJDIRS += tools

clean::
	$(RM) $(CLEANSUFFIXES:%=tools/%)

-include $(wildcard tools/*.d)
