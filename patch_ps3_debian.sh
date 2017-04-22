#!/bin/bash

# Dependencies
# sudo apt-get install git autoconf automake bison flex gcc libelf-dev make texinfo libncurses5-dev patch python-dev subversion wget zlib1g-dev ccache zip libtool-bin libgmp3-dev libssl-dev

# Fix compiling GCC 4.x with GCC 5.x (https://gcc.gnu.org/ml/gcc-patches/2015-08/msg00375.html)
cp ./patch_gcc.patch ./Autobuild/patch_gcc.patch

# Fix some things with Makeinfo in Debian + the above
cat patch_ps3sh.patch | patch -p1
