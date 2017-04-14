TOOLS = qt-faststart trasher uncoded_frame
TOOLS-$(CONFIG_ZLIB) += cws2fws

OBJDIRS += tools

clean::
	$(RM) $(CLEANSUFFIXES:%=tools/%)

-include $(wildcard tools/*.d)
