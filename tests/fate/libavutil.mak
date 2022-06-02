FATE_LIBAVUTIL += fate-adler32
fate-adler32: libavutil/tests/adler32$(EXESUF)
fate-adler32: CMD = run libavutil/tests/adler32$(EXESUF)
fate-adler32: CMP = null

FATE_LIBAVUTIL += fate-aes
fate-aes: libavutil/tests/aes$(EXESUF)
fate-aes: CMD = run libavutil/tests/aes$(EXESUF)
fate-aes: CMP = null

FATE_LIBAVUTIL += fate-aes_ctr
fate-aes_ctr: libavutil/tests/aes_ctr$(EXESUF)
fate-aes_ctr: CMD = run libavutil/tests/aes_ctr$(EXESUF)
fate-aes_ctr: CMP = null

FATE_LIBAVUTIL += fate-camellia
fate-camellia: libavutil/tests/camellia$(EXESUF)
fate-camellia: CMD = run libavutil/tests/camellia$(EXESUF)
fate-camellia: CMP = null

FATE_LIBAVUTIL += fate-cast5
fate-cast5: libavutil/tests/cast5$(EXESUF)
fate-cast5: CMD = run libavutil/tests/cast5$(EXESUF)
fate-cast5: CMP = null

FATE_LIBAVUTIL += fate-channel_layout
fate-channel_layout: libavutil/tests/channel_layout$(EXESUF)
fate-channel_layout: CMD = run libavutil/tests/channel_layout$(EXESUF)

FATE_LIBAVUTIL += fate-audio_fifo
fate-audio_fifo: libavutil/tests/audio_fifo$(EXESUF)
fate-audio_fifo: CMD = run libavutil/tests/audio_fifo$(EXESUF)

FATE_LIBAVUTIL += fate-avstring
fate-avstring: libavutil/tests/avstring$(EXESUF)
fate-avstring: CMD = run libavutil/tests/avstring$(EXESUF)

FATE_LIBAVUTIL += fate-base64
fate-base64: libavutil/tests/base64$(EXESUF)
fate-base64: CMD = run libavutil/tests/base64$(EXESUF)

FATE_LIBAVUTIL += fate-blowfish
fate-blowfish: libavutil/tests/blowfish$(EXESUF)
fate-blowfish: CMD = run libavutil/tests/blowfish$(EXESUF)

FATE_LIBAVUTIL += fate-bprint
fate-bprint: libavutil/tests/bprint$(EXESUF)
fate-bprint: CMD = run libavutil/tests/bprint$(EXESUF)

FATE_LIBAVUTIL += fate-cpu
fate-cpu: libavutil/tests/cpu$(EXESUF)
fate-cpu: CMD = runecho libavutil/tests/cpu$(EXESUF) $(CPUFLAGS:%=-c%) $(THREADS:%=-t%)
fate-cpu: CMP = null

FATE_LIBAVUTIL-$(HAVE_THREADS) += fate-cpu_init
fate-cpu_init: libavutil/tests/cpu_init$(EXESUF)
fate-cpu_init: CMD = run libavutil/tests/cpu_init$(EXESUF)
fate-cpu_init: CMP = null

FATE_LIBAVUTIL += fate-crc
fate-crc: libavutil/tests/crc$(EXESUF)
fate-crc: CMD = run libavutil/tests/crc$(EXESUF)

FATE_LIBAVUTIL += fate-color_utils
fate-color_utils: libavutil/tests/color_utils$(EXESUF)
fate-color_utils: CMD = run libavutil/tests/color_utils$(EXESUF)

FATE_LIBAVUTIL += fate-des
fate-des: libavutil/tests/des$(EXESUF)
fate-des: CMD = run libavutil/tests/des$(EXESUF)
fate-des: CMP = null

FATE_LIBAVUTIL += fate-dict
fate-dict: libavutil/tests/dict$(EXESUF)
fate-dict: CMD = run libavutil/tests/dict$(EXESUF)

FATE_LIBAVUTIL += fate-encryption-info
fate-encryption-info: libavutil/tests/encryption_info$(EXESUF)
fate-encryption-info: CMD = run libavutil/tests/encryption_info$(EXESUF)
fate-encryption-info: CMP = null

FATE_LIBAVUTIL += fate-eval
fate-eval: libavutil/tests/eval$(EXESUF)
fate-eval: CMD = run libavutil/tests/eval$(EXESUF)

FATE_LIBAVUTIL += fate-fifo
fate-fifo: libavutil/tests/fifo$(EXESUF)
fate-fifo: CMD = run libavutil/tests/fifo$(EXESUF)

FATE_LIBAVUTIL += fate-hash
fate-hash: libavutil/tests/hash$(EXESUF)
fate-hash: CMD = run libavutil/tests/hash$(EXESUF)

FATE_LIBAVUTIL += fate-hmac
fate-hmac: libavutil/tests/hmac$(EXESUF)
fate-hmac: CMD = run libavutil/tests/hmac$(EXESUF)

FATE_LIBAVUTIL += fate-imgutils
fate-imgutils: libavutil/tests/imgutils$(EXESUF)
fate-imgutils: CMD = run libavutil/tests/imgutils$(EXESUF)

FATE_LIBAVUTIL += fate-integer
fate-integer: libavutil/tests/integer$(EXESUF)
fate-integer: CMD = run libavutil/tests/integer$(EXESUF)
fate-integer: CMP = null

FATE_LIBAVUTIL += fate-lfg
fate-lfg: libavutil/tests/lfg$(EXESUF)
fate-lfg: CMD = run libavutil/tests/lfg$(EXESUF)

FATE_LIBAVUTIL += fate-md5
fate-md5: libavutil/tests/md5$(EXESUF)
fate-md5: CMD = run libavutil/tests/md5$(EXESUF)

FATE_LIBAVUTIL += fate-murmur3
fate-murmur3: libavutil/tests/murmur3$(EXESUF)
fate-murmur3: CMD = run libavutil/tests/murmur3$(EXESUF)

FATE_LIBAVUTIL += fate-parseutils
fate-parseutils: libavutil/tests/parseutils$(EXESUF)
fate-parseutils: CMD = run libavutil/tests/parseutils$(EXESUF)

FATE_LIBAVUTIL-$(CONFIG_PIXELUTILS) += fate-pixelutils
fate-pixelutils: libavutil/tests/pixelutils$(EXESUF)
fate-pixelutils: CMD = run libavutil/tests/pixelutils$(EXESUF)

FATE_LIBAVUTIL += fate-pixfmt_best
fate-pixfmt_best: libavutil/tests/pixfmt_best$(EXESUF)
fate-pixfmt_best: CMD = run libavutil/tests/pixfmt_best$(EXESUF)

FATE_LIBAVUTIL += fate-display
fate-display: libavutil/tests/display$(EXESUF)
fate-display: CMD = run libavutil/tests/display$(EXESUF)

FATE_LIBAVUTIL += fate-random_seed
fate-random_seed: libavutil/tests/random_seed$(EXESUF)
fate-random_seed: CMD = run libavutil/tests/random_seed$(EXESUF)

FATE_LIBAVUTIL += fate-ripemd
fate-ripemd: libavutil/tests/ripemd$(EXESUF)
fate-ripemd: CMD = run libavutil/tests/ripemd$(EXESUF)

FATE_LIBAVUTIL += fate-sha
fate-sha: libavutil/tests/sha$(EXESUF)
fate-sha: CMD = run libavutil/tests/sha$(EXESUF)

FATE_LIBAVUTIL += fate-sha512
fate-sha512: libavutil/tests/sha512$(EXESUF)
fate-sha512: CMD = run libavutil/tests/sha512$(EXESUF)

FATE_LIBAVUTIL += fate-tree
fate-tree: libavutil/tests/tree$(EXESUF)
fate-tree: CMD = run libavutil/tests/tree$(EXESUF)
fate-tree: CMP = null

FATE_LIBAVUTIL += fate-twofish
fate-twofish: libavutil/tests/twofish$(EXESUF)
fate-twofish: CMD = run libavutil/tests/twofish$(EXESUF)
fate-twofish: CMP = null

FATE_LIBAVUTIL += fate-xtea
fate-xtea: libavutil/tests/xtea$(EXESUF)
fate-xtea: CMD = run libavutil/tests/xtea$(EXESUF)

FATE_LIBAVUTIL += fate-tea
fate-tea: libavutil/tests/tea$(EXESUF)
fate-tea: CMD = run libavutil/tests/tea$(EXESUF)

FATE_LIBAVUTIL += fate-opt
fate-opt: libavutil/tests/opt$(EXESUF)
fate-opt: CMD = run libavutil/tests/opt$(EXESUF)

FATE_LIBAVUTIL += fate-uuid
fate-uuid: libavutil/tests/uuid$(EXESUF)
fate-uuid: CMD = run libavutil/tests/uuid$(EXESUF)
fate-uuid: CMP = null

FATE_LIBAVUTIL += $(FATE_LIBAVUTIL-yes)
FATE-$(CONFIG_AVUTIL) += $(FATE_LIBAVUTIL)
fate-libavutil: $(FATE_LIBAVUTIL)
