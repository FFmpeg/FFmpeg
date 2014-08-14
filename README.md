FFmpeg README
=============

1) Documentation
----------------

* Read the documentation in the doc/ directory in git.

  You can also view it online at http://ffmpeg.org/documentation.html

2) Licensing
------------

* See the LICENSE file.

3) Build and Install
--------------------

* See the INSTALL file.

4) SIXEL Integration
--------------------

```
$ git clone https://github.com/saitoha/libsixel
$ cd libsixel
$ git checkout abi-1.0.0
$ ./configure && make install
$ cd ..
$ git clone https://github.com/saitoha/FFmpeg-SIXEL
$ cd FFmpeg-SIXEL
$ ./configure --enable-libquvi --enable-libsixel
$ make install
$ ffmpeg -i 'https://www.youtube.com/watch?v=ixaMZPPmVG0' -f sixel -pix_fmt rgb24 -s 480x270 - 
```
