Showtime mediaplayer
====================

(c) 2006 - 2011 Andreas Öman, et al.

Settings are stored in `~/.hts/showtime`

For more information and latest versions, please visit:
[http://www.lonelycoder.com/hts/](http://www.lonelycoder.com/hts/)

## How to build for Linux

First you need to satisfy some dependencies:
in ubuntu nattyo (11.04)  

	sudo apt-get install libfreetype6-dev libfontconfig1-dev libxext-dev libgl1-mesa-dev 
	libasound2-dev libasound2-dev libgtk2.0-dev libxss-dev libxxf86vm-dev libxv-dev libcdio-cdda-dev 
	libcddb2-dev libvdpau-dev yasm


Then you need to configure:

	./configure

If any dependencies are missing the configure script will complain.
You then have the option to disable that particular module/subsystem.

	make

Build the binary, after build the binary resides in `build.linux/`.
Thus, to start it, just type:

	build.linux/showtime

If you need/want to build with a recent version of libav without
installing it on your system:

Create an libav directory somewhere, perhaps in your home dir:

	cd
	mkdir libav
	cd libav
	git clone git://git.libav.org/libav.git src

Configure libav to build and install itself in current dir. Note that this
will build static libraries so you don't need to mess around with
`LD_LIBRARY_PATH` when running the binary. IF you enable shared libraries,
remember to set that up as well.

So while still in the same dir, do

	src/configure --prefix=${PWD}
	make -j4
	make install

Now go back and reconfigure Showtime with `PKG_CONFIG_PATH` set to the
directories where the .pc files resides from the libav install:

	cd showtime 
	./configure --pkg-config-path=${HOME}/libav/lib/pkgconfig
	make


## How to build for Mac OS X

Install Xcode which includes Xcode IDE, gcc toolchain and much more. iPhone SDK also
includes Xcode and toolchain.

Install [MacPorts](http://www.macports.org)

Install pkg-config using MacPorts

	$ sudo port install pkgconfig

Now there are two possible ways to get a build environment, the MacPorts way
or the custome build scripts way. If you dont plan to build for different
architectures or SDKs as you are current running, or dont plan to compile with
fancy extensions, i would recommend the MacPorts way.

If you choose the custome script way, please continue to read support/osx/README

MacPorts way:

Install freetype and libav using MacPorts:

	$ sudo /opt/local/bin/port install freetype ffmpeg-devel

	(there is no libav port when i write this but i hope it works with the
	 ffmpeg-devel port)

Now run configure

	$ ./configure

Or if you build for release

	$ ./configure --release

If configured successfully run:

	$ make

Run showtime binary from build directory

	$ build.osx/Showtime.app/Contents/MacOS/showtime

Run showtime application from build directory

	$ open build.osx/Showtime.app

Optionally you can build Showtime.dmg disk image. Note that you should
configure with `--release` to embed theme files or else the binary will
include paths to your local build tree.

	$ make Showtime.dmg

For more information read support/osx/README

TODO: universal binary, cant be done i one step as libav does not
build when using multiple arch arguments to gcc


## How to build for Nintendo Wii

For a Wii build you need:

-  devkitPro:
   -   devkitPPC r21
   -   libogc 1.8.3
   -   libfat-ogc 1.0.5
-  freetype cross-compiled for PPC.

For your convenience there is a script that will download/build all
you need.  To run it just type:

	$ support/wiisetup

Do this directly from the showtime root directory. This will download,
unpack, build and install all that's needed into a wiisupport/
directory. By default configure.wii will look into these directories
for devkitPro and freetype, so all you have to do now is:

	$ ./configure.wii
	$ make

If you have devkitpro and/or freetype someplace else, you can set
the path to them in configure.wii (see `./configure.wii --help` for details)

Note: libogc defaults to maximum of 16 threads.
This is on the edge for showtime. Therefore, the wiisetup script will
install a new version of lwp_config.h (see `support/lwp_config.h`) before
compiling libogc. If you intend to use a stock libogc you need to be aware
of this fact.


### Wiiload

If you have [wiiload](http://wiibrew.org/wiki/Wiiload)
installed and homebrew channel is running on your wii, you can just type:

	$ make run

To start showtime on your wii.

### Homebrew package

The makefile system can build a homebrew package. To do this, type:

	$ make homebrew

The output will reside in `build.wii/bundle/`. Both an app directory
and a zip file is generated.


## How to build for PS3 with PSL1GHT

You need the opensource PS3 toolchain. Follow the instructions at:
[https://github.com/ps3dev/ps3toolchain](https://github.com/ps3dev/ps3toolchain)

If you install it from scratch when you read this the stuff it downloads
should be up to date. If you already have the toolchain and psl1ght
you need to make sure that psl1ght is at least from Tue Feb 15 2011

Once setup you need to build dependencies

	$ support/ps3setup

Then configure

	$ ./configure.ps3

.. and build

	$ make -j8

.. to generate pkgs, etc and install in `${PS3INSTALL}`

	$ make install

There is a small utility in support/traceprint that will receive UDP
packets on port 4000 and print it to stdout (only tested on Linux)
You can use this program to get debug output when running the app
on ps3. configure with `--logtarget=IP.OF.YOUR.HOST` to make it send
log messages to that machine. Showtime will default to port 4000 when
sending log messages.
