# License

Most files in FFmpeg are under the GNU Lesser General Public License version 2.1
or later (LGPL v2.1+). Read the file `COPYING.LGPLv2.1` for details. Some other
files have MIT/X11/BSD-style licenses. In combination the LGPL v2.1+ applies to
FFmpeg.

Some optional parts of FFmpeg are licensed under the GNU General Public License
version 2 or later (GPL v2+). See the file `COPYING.GPLv2` for details. None of
these parts are used by default, you have to explicitly pass `--enable-gpl` to
configure to activate them. In this case, FFmpeg's license changes to GPL v2+.

Specifically, the GPL parts of FFmpeg are:

- libpostproc
- optional x86 optimization in the files
    - `libavcodec/x86/flac_dsp_gpl.asm`
    - `libavcodec/x86/idct_mmx.c`
    - `libavfilter/x86/vf_removegrain.asm`
- the X11 grabber in `libavdevice/x11grab.c`
- the following building and testing tools
    - `compat/solaris/make_sunver.pl`
    - `doc/t2h.pm`
    - `doc/texi2pod.pl`
    - `libswresample/swresample-test.c`
    - `tests/checkasm/*`
    - `tests/tiny_ssim.c`
- the following filters in libavfilter:
    - `f_ebur128.c`
    - `vf_blackframe.c`
    - `vf_boxblur.c`
    - `vf_colormatrix.c`
    - `vf_cover_rect.c`
    - `vf_cropdetect.c`
    - `vf_delogo.c`
    - `vf_eq.c`
    - `vf_find_rect.c`
    - `vf_fspp.c`
    - `vf_geq.c`
    - `vf_histeq.c`
    - `vf_hqdn3d.c`
    - `vf_interlace.c`
    - `vf_kerndeint.c`
    - `vf_mcdeint.c`
    - `vf_mpdecimate.c`
    - `vf_owdenoise.c`
    - `vf_perspective.c`
    - `vf_phase.c`
    - `vf_pp.c`
    - `vf_pp7.c`
    - `vf_pullup.c`
    - `vf_repeatfields.c`
    - `vf_sab.c`
    - `vf_smartblur.c`
    - `vf_spp.c`
    - `vf_stereo3d.c`
    - `vf_super2xsai.c`
    - `vf_tinterlace.c`
    - `vf_uspp.c`
    - `vsrc_mptestsrc.c`

Should you, for whatever reason, prefer to use version 3 of the (L)GPL, then
the configure parameter `--enable-version3` will activate this licensing option
for you. Read the file `COPYING.LGPLv3` or, if you have enabled GPL parts,
`COPYING.GPLv3` to learn the exact legal terms that apply in this case.

There are a handful of files under other licensing terms, namely:

* The files `libavcodec/jfdctfst.c`, `libavcodec/jfdctint_template.c` and
  `libavcodec/jrevdct.c` are taken from libjpeg, see the top of the files for
  licensing details. Specifically note that you must credit the IJG in the
  documentation accompanying your program if you only distribute executables.
  You must also indicate any changes including additions and deletions to
  those three files in the documentation.
* `tests/reference.pnm` is under the expat license.


## External libraries

FFmpeg can be combined with a number of external libraries, which sometimes
affect the licensing of binaries resulting from the combination.

### Compatible libraries

The following libraries are under GPL:
- frei0r
- libcdio
- librubberband
- libvidstab
- libx264
- libx265
- libxavs
- libxvid

When combining them with FFmpeg, FFmpeg needs to be licensed as GPL as well by
passing `--enable-gpl` to configure.

The OpenCORE and VisualOn libraries are under the Apache License 2.0. That
license is incompatible with the LGPL v2.1 and the GPL v2, but not with
version 3 of those licenses. So to combine these libraries with FFmpeg, the
license version needs to be upgraded by passing `--enable-version3` to configure.

### Incompatible libraries

There are certain libraries you can combine with FFmpeg whose licenses are not
compatible with the GPL and/or the LGPL. If you wish to enable these
libraries, even in circumstances that their license may be incompatible, pass
`--enable-nonfree` to configure. But note that if you enable any of these
libraries the resulting binary will be under a complex license mix that is
more restrictive than the LGPL and that may result in additional obligations.
It is possible that these restrictions cause the resulting binary to be
unredistributable.

The Fraunhofer FDK AAC and OpenSSL libraries are under licenses which are
incompatible with the GPLv2 and v3. To the best of our knowledge, they are
compatible with the LGPL.

The NVENC library, while its header file is licensed under the compatible MIT
license, requires a proprietary binary blob at run time, and is deemed to be
incompatible with the GPL. We are not certain if it is compatible with the
LGPL, but we require `--enable-nonfree` even with LGPL configurations in case
it is not.
