CHECKASMOBJS-$(HAVE_YASM) += x86/checkasm.o

tests/checkasm/x86/%.o: tests/checkasm/x86/%.asm
	$(DEPYASM) $(YASMFLAGS) -I $(<D)/ -M -o $@ $< > $(@:.o=.d)
	$(YASM) $(YASMFLAGS) -I $(<D)/ -o $@ $<
	-$(STRIP) $(STRIPFLAGS) $@
