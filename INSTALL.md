## Installing FFmpeg

0. If you like to include source plugins, merge them before configure
for example run tools/merge-all-source-plugins

1. Type `./configure` to create the configuration. A list of configure
options is printed by running `configure --help`.

    `configure` can be launched from a directory different from the FFmpeg
sources to build the objects out of tree. To do this, use an absolute
path when launching `configure`, e.g. `/ffmpegdir/ffmpeg/configure`.

2. Then type `make` to build FFmpeg. GNU Make 3.81 or later is required.

3. Type `make install` to install all binaries and libraries you built.

NOTICE
------

 - Non system dependencies (e.g. libx264, libvpx) are disabled by default.

NOTICE for Package Maintainers
------------------------------

 - It is recommended to build FFmpeg twice, first with minimal external dependencies so
   that 3rd party packages, which depend on FFmpegs libavutil/libavfilter/libavcodec/libavformat
   can then be built. And last build FFmpeg with full dependancies (which may in turn depend on
   some of these 3rd party packages). This avoids circular dependencies during build.
