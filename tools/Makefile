TOOLS = qt-faststart trasher uncoded_frame
TOOLS-$(CONFIG_LIBMYSOFA) += sofa2wavs
TOOLS-$(CONFIG_ZLIB) += cws2fws

tools/target_dec_%_fuzzer.o: tools/target_dec_fuzzer.c
	$(COMPILE_C) -DFFMPEG_DECODER=$*

tools/target_bsf_%_fuzzer.o: tools/target_bsf_fuzzer.c
	$(COMPILE_C) -DFFMPEG_BSF=$*

tools/target_dem_fuzzer.o: tools/target_dem_fuzzer.c
	$(COMPILE_C)

OUTDIRS += tools

clean::
	$(RM) $(CLEANSUFFIXES:%=tools/%)

-include $(wildcard tools/*.d)
