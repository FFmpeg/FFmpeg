# KJSL build everything on Linux...

git clone https://github.com/kevleyski/FFmpeg.git ~/ffmpeg_sources

#nasm

cd ~/ffmpeg_sources
curl -O -L http://www.nasm.us/pub/nasm/releasebuilds/2.13.02/nasm-2.13.02.tar.bz2
tar xjvf nasm-2.13.02.tar.bz2
cd nasm-2.13.02
./autogen.sh
./configure --prefix="$HOME/ffmpeg_build" --bindir="$HOME/bin"
make
make install

#Yasm
#An assembler used by some libraries. Highly recommended or your resulting build may be very slow.

cd ~/ffmpeg_sources
curl -O -L http://www.tortall.net/projects/yasm/releases/yasm-1.3.0.tar.gz
tar xzvf yasm-1.3.0.tar.gz
cd yasm-1.3.0
./configure --prefix="$HOME/ffmpeg_build" --bindir="$HOME/bin"
make
make install
libx264

# H.264 video encoder. See the H.264 Encoding Guide for more information and usage examples.

#Requires ffmpeg to be configured with --enable-gpl --enable-libx264.

cd ~/ffmpeg_sources
git clone --depth 1 http://git.videolan.org/git/x264
cd x264
PKG_CONFIG_PATH="$HOME/ffmpeg_build/lib/pkgconfig" ./configure --prefix="$HOME/ffmpeg_build" --bindir="$HOME/bin" --enable-static
make
make install
#Warning: If you get Found no assembler. Minimum version is nasm-2.13 or similar after running ./configure then the outdated nasm package from the repo is installed. Run yum remove nasm && hash -r and x264 will then use your newly compiled nasm instead. Ensure environment is able to resolve path to nasm binary.

#libx265
#H.265/HEVC video encoder. See the H.265 Encoding Guide for more information and usage examples.

#Requires ffmpeg to be configured with --enable-gpl --enable-libx265.

cd ~/ffmpeg_sources
hg clone https://bitbucket.org/multicoreware/x265
cd ~/ffmpeg_sources/x265/build/linux
cmake -G "Unix Makefiles" -DCMAKE_INSTALL_PREFIX="$HOME/ffmpeg_build" -DENABLE_SHARED:bool=off ../../source
make
make install

#libfdk_aac
#AAC audio encoder. See the AAC Audio Encoding Guide for more information and usage examples.

#Requires ffmpeg to be configured with --enable-libfdk_aac (and --enable-nonfree if you also included --enable-gpl).

cd ~/ffmpeg_sources
git clone --depth 1 https://github.com/mstorsjo/fdk-aac
cd fdk-aac
autoreconf -fiv
./configure --prefix="$HOME/ffmpeg_build" --disable-shared
make
make install
libmp3lame
MP3 audio encoder.

#Requires ffmpeg to be configured with --enable-libmp3lame.

cd ~/ffmpeg_sources
curl -O -L http://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz
tar xzvf lame-3.100.tar.gz
cd lame-3.100
./configure --prefix="$HOME/ffmpeg_build" --bindir="$HOME/bin" --disable-shared --enable-nasm
make
make install

#libopus
#Opus audio decoder and encoder.

#Requires ffmpeg to be configured with --enable-libopus.

cd ~/ffmpeg_sources
curl -O -L https://archive.mozilla.org/pub/opus/opus-1.2.1.tar.gz
tar xzvf opus-1.2.1.tar.gz
cd opus-1.2.1
./configure --prefix="$HOME/ffmpeg_build" --disable-shared
make
make install

#libogg
#Ogg bitstream library. Required by libtheora and libvorbis.

cd ~/ffmpeg_sources
curl -O -L http://downloads.xiph.org/releases/ogg/libogg-1.3.3.tar.gz
tar xzvf libogg-1.3.3.tar.gz
cd libogg-1.3.3
./configure --prefix="$HOME/ffmpeg_build" --disable-shared
make
make install

#libvorbis
#Vorbis audio encoder. Requires libogg.

#Requires ffmpeg to be configured with --enable-libvorbis.

cd ~/ffmpeg_sources
curl -O -L http://downloads.xiph.org/releases/vorbis/libvorbis-1.3.5.tar.gz
tar xzvf libvorbis-1.3.5.tar.gz
cd libvorbis-1.3.5
./configure --prefix="$HOME/ffmpeg_build" --with-ogg="$HOME/ffmpeg_build" --disable-shared
make
make install

#libvpx
#VP8/VP9 video encoder and decoder. See the VP9 Video Encoding Guide for more information and usage examples.

#Requires ffmpeg to be configured with --enable-libvpx.

cd ~/ffmpeg_sources
git clone --depth 1 https://chromium.googlesource.com/webm/libvpx.git
cd libvpx
./configure --prefix="$HOME/ffmpeg_build" --disable-examples --disable-unit-tests --enable-vp9-highbitdepth --as=yasm
make
make install

#libaom (AV1 for x86)
cd ~/ffmpeg_sources
curl -O -L https://github.com/kevleyski/aom/archive/master.zip
unzip master.zip
cd aom-master
./configure --prefix="$HOME/ffmpeg_build" --enable-av1 --disable-shared
make
make install
cd ..


# FFmpeg
cd ~/ffmpeg_sources
git clone https://github.com/kevleyski/FFmpeg.git ffmpeg
cd ffmpeg
PATH="$HOME/bin:$PATH" PKG_CONFIG_PATH="$HOME/ffmpeg_build/lib/pkgconfig" ./configure \
  --prefix="$HOME/ffmpeg_build" \
  --pkg-config-flags="--static" \
  --extra-cflags="-I$HOME/ffmpeg_build/include" \
  --extra-ldflags="-L$HOME/ffmpeg_build/lib" \
  --extra-libs=-lpthread \
  --extra-libs=-lm \
  --bindir="$HOME/bin" \
  --enable-gpl \
  --enable-libfdk_aac \
  --enable-libfreetype \
  --enable-libmp3lame \
  --enable-libopus \
  --enable-libvorbis \
  --enable-libvpx \
  --enable-libx264 \
  --enable-libx265 \
  --enable-openssl \
  --enable-libaom \
  --enable-nonfree
make
make install
hash -r
