TOOLS = qt-faststart trasher uncoded_frame
TOOLS-$(CONFIG_ZLIB) += cws2fws

tools/target_dec_video_%_fuzzer.o: tools/target_dec_fuzzer.c
	$(COMPILE_C) -DFFMPEG_CODEC=AV_CODEC_ID_$* -DFUZZ_FFMPEG_VIDEO

tools/target_dec_audio_%_fuzzer.o: tools/target_dec_fuzzer.c
	$(COMPILE_C) -DFFMPEG_CODEC=AV_CODEC_ID_$* -DFUZZ_FFMPEG_AUDIO

tools/target_dec_subtitle_%_fuzzer.o: tools/target_dec_fuzzer.c
	$(COMPILE_C) -DFFMPEG_CODEC=AV_CODEC_ID_$* -DFUZZ_FFMPEG_SUBTITLE


OBJDIRS += tools

clean::
	$(RM) $(CLEANSUFFIXES:%=tools/%)

-include $(wildcard tools/*.d)
