TOOLS = enum_options qt-faststart scale_slice_test trasher uncoded_frame
TOOLS-$(CONFIG_LIBMYSOFA) += sofa2wavs
TOOLS-$(CONFIG_ZLIB) += cws2fws

tools/target_dec_%_fuzzer.o: tools/target_dec_fuzzer.c
	$(COMPILE_C) -DFFMPEG_DECODER=$*

tools/target_bsf_%_fuzzer.o: tools/target_bsf_fuzzer.c
	$(COMPILE_C) -DFFMPEG_BSF=$*

tools/target_dem_%_fuzzer.o: tools/target_dem_fuzzer.c
	$(COMPILE_C) -DFFMPEG_DEMUXER=$* -DIO_FLAT=0

tools/target_dem_fuzzer.o: tools/target_dem_fuzzer.c
	$(COMPILE_C) -DIO_FLAT=1

tools/target_io_dem_fuzzer.o: tools/target_dem_fuzzer.c
	$(COMPILE_C) -DIO_FLAT=0

tools/venc_data_dump$(EXESUF): tools/decode_simple.o
tools/scale_slice_test$(EXESUF): tools/decode_simple.o

OUTDIRS += tools

clean::
	$(RM) $(CLEANSUFFIXES:%=tools/%)

-include $(wildcard tools/*.d)
