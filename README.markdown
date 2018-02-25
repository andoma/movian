Movian mediaplayer
==================

(c) 2006 - 2016 Lonelycoder AB

[![Build status](https://doozer.io/badge/andoma/movian/buildstatus/master)](https://doozer.io/user/andoma/movian)

For more information and latest versions, please visit:

with BiDi support for Farsi and Arabic languages 
[https://movian.tv/](https://movian.tv/)

## How to build for Linux

First you need to satisfy some dependencies:
For Ubuntu 12.04)

	sudo apt-get install libfreetype6-dev libfontconfig1-dev libxext-dev libgl1-mesa-dev libasound2-dev libasound2-dev libgtk2.0-dev libxss-dev libxxf86vm-dev libxv-dev libvdpau-dev yasm libpulse-dev libssl-dev curl libwebkitgtk-dev libsqlite3-dev ccache


Then you need to configure:

	./configure

If your system lacks libwebkitgtk (Ubuntu 12.04 before 12.04.1) 
you can configure with

	./configure --disable-webkit

If any dependencies are missing the configure script will complain.
You then have the option to disable that particular module/subsystem.

	make

Build the binary, after build the binary resides in `./build.linux/`.
Thus, to start it, just type:

	./build.linux/movian

Settings are stored in `~/.hts/showtime`

If you want to build with extra debugging options for development these options might be of interest:

	--cc=gcc-5 --extra-cflags=-fno-omit-frame-pointer --optlevel=g --sanitize=address --enable-bughunt


## How to build for Mac OS X

To build for Mac OS X you need Xcode and yasm. Xcode should be installed from Mac Appstore.

To install yasm, install [Brew](http://brew.sh/) and then

	$ brew install yasm

Now run configure

	$ ./configure

Or if you build for release

	$ ./configure --release

If configured successfully run:

	$ make

Run Movian binary from build directory

	$ build.osx/Movian.app/Contents/MacOS/movian

Note that in this case Movian loads all resources from current directory
so this binary can't be run elsewhere.

If you want a build that can be run as a normal Mac Application you shold do

	$ make dist

This will generate a DMG

## How to build for PS3 with PSL1GHT

$ ./Autobuild.sh -t ps3

## How to build for Raspberry Pi

First you need to satisfy some dependencies (For Ubuntu 12.04LTS 64bit):

	sudo apt-get install git-core build-essential autoconf bison flex libelf-dev libtool pkg-config texinfo libncurses5-dev libz-dev python-dev libssl-dev libgmp3-dev ccache zip squashfs-tools

$ ./Autobuild.sh -t rpi

To update Movian on rpi with compiled one, enable Binreplace in settings:dev and issue:

	curl --data-binary @build.rpi/showtime.sqfs http://rpi_ip_address:42000/api/replace

