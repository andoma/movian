#!/bin/bash

# Fix GCC 5.x
cp ./patch_gcc.patch ./Autobuild/patch_gcc.patch

# Fix some things with 
cat patch_ps3sh.patch | patch -p1