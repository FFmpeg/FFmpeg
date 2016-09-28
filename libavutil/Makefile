include $(SUBDIR)../config.mak

NAME = avutil

HEADERS = adler32.h                                                     \
          aes.h                                                         \
          aes_ctr.h                                                     \
          attributes.h                                                  \
          audio_fifo.h                                                  \
          avassert.h                                                    \
          avstring.h                                                    \
          avutil.h                                                      \
          base64.h                                                      \
          blowfish.h                                                    \
          bprint.h                                                      \
          bswap.h                                                       \
          buffer.h                                                      \
          cast5.h                                                       \
          camellia.h                                                    \
          channel_layout.h                                              \
          common.h                                                      \
          cpu.h                                                         \
          crc.h                                                         \
          des.h                                                         \
          dict.h                                                        \
          display.h                                                     \
          downmix_info.h                                                \
          error.h                                                       \
          eval.h                                                        \
          fifo.h                                                        \
          file.h                                                        \
          frame.h                                                       \
          hash.h                                                        \
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
          mastering_display_metadata.h                                  \
          md5.h                                                         \
          mem.h                                                         \
          motion_vector.h                                               \
          murmur3.h                                                     \
          opt.h                                                         \
          parseutils.h                                                  \
          pixdesc.h                                                     \
          pixelutils.h                                                  \
          pixfmt.h                                                      \
          random_seed.h                                                 \
          rc4.h                                                         \
          rational.h                                                    \
          replaygain.h                                                  \
          ripemd.h                                                      \
          samplefmt.h                                                   \
          sha.h                                                         \
          sha512.h                                                      \
          stereo3d.h                                                    \
          threadmessage.h                                               \
          time.h                                                        \
          timecode.h                                                    \
          timestamp.h                                                   \
          tree.h                                                        \
          twofish.h                                                     \
          version.h                                                     \
          xtea.h                                                        \
          tea.h                                                         \

HEADERS-$(CONFIG_LZO)                   += lzo.h

HEADERS-$(CONFIG_OPENCL)                += opencl.h

ARCH_HEADERS = bswap.h                                                  \
               intmath.h                                                \
               intreadwrite.h                                           \
               timer.h                                                  \

BUILT_HEADERS = avconfig.h                                              \
                ffversion.h

OBJS = adler32.o                                                        \
       aes.o                                                            \
       aes_ctr.o                                                        \
       audio_fifo.o                                                     \
       avstring.o                                                       \
       base64.o                                                         \
       blowfish.o                                                       \
       bprint.o                                                         \
       buffer.o                                                         \
       cast5.o                                                          \
       camellia.o                                                       \
       channel_layout.o                                                 \
       color_utils.o                                                    \
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
       fixed_dsp.o                                                      \
       frame.o                                                          \
       hash.o                                                           \
       hmac.o                                                           \
       hwcontext.o                                                      \
       imgutils.o                                                       \
       integer.o                                                        \
       intmath.o                                                        \
       lfg.o                                                            \
       lls.o                                                            \
       log.o                                                            \
       log2_tab.o                                                       \
       mathematics.o                                                    \
       mastering_display_metadata.o                                     \
       md5.o                                                            \
       mem.o                                                            \
       murmur3.o                                                        \
       opt.o                                                            \
       parseutils.o                                                     \
       pixdesc.o                                                        \
       pixelutils.o                                                     \
       random_seed.o                                                    \
       rational.o                                                       \
       reverse.o                                                        \
       rc4.o                                                            \
       ripemd.o                                                         \
       samplefmt.o                                                      \
       sha.o                                                            \
       sha512.o                                                         \
       stereo3d.o                                                       \
       threadmessage.o                                                  \
       time.o                                                           \
       timecode.o                                                       \
       tree.o                                                           \
       twofish.o                                                        \
       utils.o                                                          \
       xga_font_data.o                                                  \
       xtea.o                                                           \
       tea.o                                                            \

OBJS-$(!HAVE_ATOMICS_NATIVE)            += atomic.o                     \

OBJS-$(CONFIG_CUDA)                     += hwcontext_cuda.o
OBJS-$(CONFIG_DXVA2)                    += hwcontext_dxva2.o
OBJS-$(CONFIG_QSV)                   += hwcontext_qsv.o
OBJS-$(CONFIG_LZO)                      += lzo.o
OBJS-$(CONFIG_OPENCL)                   += opencl.o opencl_internal.o
OBJS-$(CONFIG_VAAPI)                    += hwcontext_vaapi.o
OBJS-$(CONFIG_VDPAU)                    += hwcontext_vdpau.o

OBJS += $(COMPAT_OBJS:%=../compat/%)

# Windows resource file
SLIBOBJS-$(HAVE_GNU_WINDRES)            += avutilres.o

SKIPHEADERS-$(CONFIG_CUDA)             += hwcontext_cuda.h
SKIPHEADERS-$(CONFIG_DXVA2)            += hwcontext_dxva2.h
SKIPHEADERS-$(CONFIG_QSV)           += hwcontext_qsv.h
SKIPHEADERS-$(CONFIG_VAAPI)            += hwcontext_vaapi.h
SKIPHEADERS-$(CONFIG_VDPAU)            += hwcontext_vdpau.h
SKIPHEADERS-$(HAVE_ATOMICS_GCC)        += atomic_gcc.h
SKIPHEADERS-$(HAVE_ATOMICS_SUNCC)      += atomic_suncc.h
SKIPHEADERS-$(HAVE_ATOMICS_WIN32)      += atomic_win32.h
SKIPHEADERS-$(CONFIG_OPENCL)           += opencl.h

TESTPROGS = adler32                                                     \
            aes                                                         \
            atomic                                                      \
            avstring                                                    \
            base64                                                      \
            blowfish                                                    \
            bprint                                                      \
            cast5                                                       \
            camellia                                                    \
            color_utils                                                 \
            cpu                                                         \
            crc                                                         \
            des                                                         \
            dict                                                        \
            display                                                     \
            error                                                       \
            eval                                                        \
            file                                                        \
            fifo                                                        \
            float_dsp                                                   \
            hash                                                        \
            hmac                                                        \
            lfg                                                         \
            lls                                                         \
            log                                                         \
            md5                                                         \
            murmur3                                                     \
            opt                                                         \
            pca                                                         \
            parseutils                                                  \
            pixdesc                                                     \
            pixelutils                                                  \
            random_seed                                                 \
            rational                                                    \
            ripemd                                                      \
            sha                                                         \
            sha512                                                      \
            softfloat                                                   \
            tree                                                        \
            twofish                                                     \
            utf8                                                        \
            xtea                                                        \
            tea                                                         \

TESTPROGS-$(HAVE_LZO1X_999_COMPRESS) += lzo

TOOLS = crypto_bench ffhash ffeval ffescape

tools/crypto_bench$(EXESUF): ELIBS += $(if $(VERSUS),$(subst +, -l,+$(VERSUS)),)
tools/crypto_bench$(EXESUF): CFLAGS += -DUSE_EXT_LIBS=0$(if $(VERSUS),$(subst +,+USE_,+$(VERSUS)),)

$(SUBDIR)tests/lzo$(EXESUF): ELIBS = -llzo2
