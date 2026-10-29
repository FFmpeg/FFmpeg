OBJS        += aarch64/rgb2rgb.o                \
               aarch64/swscale.o                \
               aarch64/swscale_unscaled.o       \

NEON-OBJS   += aarch64/hscale.o                 \
               aarch64/input.o                  \
               aarch64/output.o                 \
               aarch64/range_convert_neon.o     \
               aarch64/rgb2rgb_neon.o           \
               aarch64/swscale_unscaled_neon.o  \
               aarch64/xyz2rgb_neon.o           \
               aarch64/yuv2rgb_neon.o           \

NEON-OBJS   += aarch64/ops.o                    \
               aarch64/ops_neon.gen.o           \

$(SUBDIR)aarch64/ops_neon.gen.S: $(SUBDIR)aarch64/ops_static$(HOSTEXESUF)
	$(M)$< > $@.tmp
	$(CP) $@.tmp $@
	$(RM) $@.tmp

clean::
	$(RM) $(CLEANSUFFIXES:%=libswscale/aarch64/%)

HOSTPROGS = aarch64/ops_static
