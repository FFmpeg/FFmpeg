FFmpeg for Vitamio
==================

0) Branches
-----------

- **vitamio**, the main branch for Vitamio
- **master**, sync with FFmpeg official master


1) Development
--------------

    ./configure --prefix=build
    make -j7 install
    LD_LIBRARY_PATH=build/lib/ build/bin/ffplay http://devimages.apple.com/iphone/samples/bipbop/bipbopall.m3u8

2) Android build
----------------

    ./build_android.sh

3) iOS build
-----------

		./build_ios.sh
