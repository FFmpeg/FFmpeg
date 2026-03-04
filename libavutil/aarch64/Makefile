OBJS += aarch64/cpu.o                                                 \
        aarch64/float_dsp_init.o                                      \
        aarch64/tx_float_init.o                                       \

ARMV8-OBJS += aarch64/crc.o

NEON-OBJS += aarch64/float_dsp_neon.o                                 \
             aarch64/tx_float_neon.o                                  \

NEON-OBJS-$(CONFIG_PIXELUTILS) += aarch64/pixelutils_neon.o

SVE-OBJS += aarch64/cpu_sve.o                                         \

SME-OBJS += aarch64/cpu_sme.o                                         \
