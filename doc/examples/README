FFmpeg examples README
----------------------

Both following use cases rely on pkg-config and make, thus make sure
that you have them installed and working on your system.


1) Build the installed examples in a generic read/write user directory

Copy to a read/write user directory and just use "make", it will link
to the libraries on your system, assuming the PKG_CONFIG_PATH is
correctly configured.

2) Build the examples in-tree

Assuming you are in the source FFmpeg checkout directory, you need to build
FFmpeg (no need to make install in any prefix). Then you can go into
doc/examples and run a command such as PKG_CONFIG_PATH=pc-uninstalled make.
