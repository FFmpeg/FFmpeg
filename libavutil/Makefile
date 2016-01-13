NAME = avutil

HEADERS = adler32.h                                                     \
          aes.h                                                         \
          attributes.h                                                  \
          audio_fifo.h                                                  \
          avassert.h                                                    \
          avstring.h                                                    \
          avutil.h                                                      \
          base64.h                                                      \
          blowfish.h                                                    \
          bswap.h                                                       \
          buffer.h                                                      \
          channel_layout.h                                              \
          common.h                                                      \
          cpu.h                                                         \
          crc.h                                                         \
          dict.h                                                        \
          display.h                                                     \
          downmix_info.h                                                \
          error.h                                                       \
          eval.h                                                        \
          fifo.h                                                        \
          file.h                                                        \
          frame.h                                                       \
          hmac.h                                                        \
          hwcontext.h                                                   \
          hwcontext_cuda.h                                              \
          hwcontext_dxva2.h                                             \
          hwcontext_qsv.h                                               \
          hwcontext_vaapi.h                                             \
          hwcontext_vdpau.h                                             \
          imgutils.h                                                    \
          intfloat.h                                                    \
          intreadwrite.h                                                \
          lfg.h                                                         \
          log.h                                                         \
          macros.h                                                      \
          mathematics.h                                                 \
          md5.h                                                         \
          mem.h                                                         \
          opt.h                                                         \
          parseutils.h                                                  \
          pixdesc.h                                                     \
          pixfmt.h                                                      \
          random_seed.h                                                 \
          rational.h                                                    \
          replaygain.h                                                  \
          samplefmt.h                                                   \
          sha.h                                                         \
          stereo3d.h                                                    \
          time.h                                                        \
          version.h                                                     \
          xtea.h                                                        \

HEADERS-$(CONFIG_LZO)                   += lzo.h

ARCH_HEADERS = bswap.h                                                  \
               intmath.h                                                \
               intreadwrite.h                                           \
               timer.h                                                  \

BUILT_HEADERS = avconfig.h

OBJS = adler32.o                                                        \
       aes.o                                                            \
       atomic.o                                                         \
       audio_fifo.o                                                     \
       avstring.o                                                       \
       base64.o                                                         \
       blowfish.o                                                       \
       buffer.o                                                         \
       channel_layout.o                                                 \
       cpu.o                                                            \
       crc.o                                                            \
       des.o                                                            \
       dict.o                                                           \
       display.o                                                        \
       downmix_info.o                                                   \
       error.o                                                          \
       eval.o                                                           \
       fifo.o                                                           \
       file.o                                                           \
       file_open.o                                                      \
       float_dsp.o                                                      \
       frame.o                                                          \
       hmac.o                                                           \
       hwcontext.o                                                      \
       imgutils.o                                                       \
       intmath.o                                                        \
       lfg.o                                                            \
       lls.o                                                            \
       log.o                                                            \
       log2_tab.o                                                       \
       mathematics.o                                                    \
       md5.o                                                            \
       mem.o                                                            \
       opt.o                                                            \
       parseutils.o                                                     \
       pixdesc.o                                                        \
       random_seed.o                                                    \
       rational.o                                                       \
       rc4.o                                                            \
       samplefmt.o                                                      \
       sha.o                                                            \
       stereo3d.o                                                       \
       time.o                                                           \
       tree.o                                                           \
       utils.o                                                          \
       xtea.o                                                           \

OBJS-$(CONFIG_CUDA)                     += hwcontext_cuda.o
OBJS-$(CONFIG_DXVA2)                    += hwcontext_dxva2.o
OBJS-$(CONFIG_LIBMFX)                   += hwcontext_qsv.o
OBJS-$(CONFIG_LZO)                      += lzo.o
OBJS-$(CONFIG_VAAPI)                    += hwcontext_vaapi.o
OBJS-$(CONFIG_VDPAU)                    += hwcontext_vdpau.o

OBJS += $(COMPAT_OBJS:%=../compat/%)

SKIPHEADERS-$(CONFIG_CUDA)             += hwcontext_cuda.h
SKIPHEADERS-$(CONFIG_DXVA2)            += hwcontext_dxva2.h
SKIPHEADERS-$(CONFIG_LIBMFX)           += hwcontext_qsv.h
SKIPHEADERS-$(CONFIG_VAAPI)            += hwcontext_vaapi.h
SKIPHEADERS-$(CONFIG_VDPAU)            += hwcontext_vdpau.h
SKIPHEADERS-$(HAVE_ATOMICS_GCC)        += atomic_gcc.h
SKIPHEADERS-$(HAVE_ATOMICS_SUNCC)      += atomic_suncc.h
SKIPHEADERS-$(HAVE_ATOMICS_WIN32)      += atomic_win32.h

TESTPROGS = adler32                                                     \
            aes                                                         \
            atomic                                                      \
            avstring                                                    \
            base64                                                      \
            blowfish                                                    \
            cpu                                                         \
            crc                                                         \
            des                                                         \
            eval                                                        \
            fifo                                                        \
            float_dsp                                                   \
            hmac                                                        \
            lfg                                                         \
            lls                                                         \
            md5                                                         \
            opt                                                         \
            parseutils                                                  \
            sha                                                         \
            tree                                                        \
            xtea                                                        \
