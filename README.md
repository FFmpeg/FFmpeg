FFmpeg README
=============

0) Branches
-----------

- **vplayer**, the main branch for VPlayer
- **master**, sync with FFmpeg official master


1) Development
--------------

    ./configure --prefix=build
    make -j7 install
    LD_LIBRARY_PATH=build/lib/ build/bin/ffplay cache:/tmp/vvvvvvv:http://web.qqvideo.tc.qq.com/s0012pqc3z1.flv


2) Android build
----------------

    ./Android.sh
