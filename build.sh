#!/bin/bash
make -j8
sudo make install
sudo rm /usr/local/lib/libav*
sudo cp ./*/*.so* /usr/local/lib
