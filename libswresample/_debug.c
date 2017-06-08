#include "swresample_internal.h"

struct ResampleContext;

#if !ARCH_AARCH64
void swri_audio_convert_init_aarch64(struct AudioConvert *ac, enum AVSampleFormat out_fmt, enum AVSampleFormat in_fmt, int channels){};
#endif
#if !ARCH_ARM
void swri_resample_dsp_arm_init(struct ResampleContext *c) {};
void swri_audio_convert_init_arm(struct AudioConvert *ac, enum AVSampleFormat out_fmt, enum AVSampleFormat in_fmt, int channels){};
#endif
#if !ARCH_PPC
#endif
#if !ARCH_MIPS
#endif
#if !ARCH_X86
void swri_resample_dsp_x86_init(struct ResampleContext *c) {};
void swri_audio_convert_init_x86(struct AudioConvert *ac, enum AVSampleFormat out_fmt, enum AVSampleFormat in_fmt, int channels){};
#endif

#if !ARCH_X86_32
int ff_resample_common_int16_mmxext(struct ResampleContext *c, void *dst, \
                                      const void *src, int sz, int upd) {return 0;};
int ff_resample_linear_int16_mmxext(struct ResampleContext *c, void *dst, \
                                      const void *src, int sz, int upd) {return 0;};
#endif