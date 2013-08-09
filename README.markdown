Showtime mediaplayer
====================

(c) 2006 - 2011 Andreas Öman, et al.

Settings are stored in `~/.hts/showtime`

For more information and latest versions, please visit:
[http://www.lonelycoder.com/hts/](http://www.lonelycoder.com/hts/)

## How to build for Linux

First you need to satisfy some dependencies:
For Ubuntu 12.04)  

	sudo apt-get install libfreetype6-dev libfontconfig1-dev libxext-dev libgl1-mesa-dev 
	libasound2-dev libasound2-dev libgtk2.0-dev libxss-dev libxxf86vm-dev libxv-dev libcdio-cdda-dev 
	libcddb2-dev libvdpau-dev yasm libpulse-dev libssl-dev curl
	libwebkitgtk-dev libsqlite3-dev

Then you need to configure:

	./configure

If your system lacks libwebkitgtk (Ubunut 12.04 before 12.04.1) 
you can configure with

        ./configure --disable-webkit

If any dependencies are missing the configure script will complain.
You then have the option to disable that particular module/subsystem.

	make

Build the binary, after build the binary resides in `build.linux/`.
Thus, to start it, just type:

	build.linux/showtime


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

NOTE: Wii build is currently unmaintained and does not work

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

You also need correct version of PSL1GHT from [https://github.com/andoma/PSL1GHT](https://github.com/andoma/PSL1GHT)

If you install it from scratch when you read this the stuff it downloads
should be up to date. If you already have the toolchain and psl1ght
you need to make sure that psl1ght is at least from Tue Feb 15 2011

Configure

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

## How to build for Raspberry Pi

Showtime for Raspberry Pi can only be built using cross compilation
on a "normal" Linux host. In order word you will compile it on your
normal Linux desktop system and then copy the binary to the Raspberry Pi.

If you just want to create a build to test with you can cheat a bit
and use the same technique as the auto build system will do.

Note that this requires a host that is 64 bit (due to how the
toolchains are compiled)

Check out the source from github on your normal Linux desktop (ie.
not the Raspberry Pi) and do

	cd showtime
	./Autobuild.sh -t rpi

This will start off and download all necessary components to build Showtime
and will eventually start building it as well. It will take some time.

Once completed a binary will end up here: build.rpi/showtime

Copy this file to the Raspberry Pi and run it.

Note: It cannot run when X is running. You should boot raspbian
into console mode and start it there

Note2: If you want Spotify support you need to copy libspotify.so.12 from

       build.rpi/libspotify-12.1.103-Linux-armv6-bcm2708hardfp-release/lib/libspotify.so.12

into your users home directory on the Rpi (usually /home/rpi). (Installing
the Application from the Apps section in Showtime is not enough)
