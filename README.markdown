Showtime mediaplayer
====================

(c) 2006 - 2013 Andreas Öman


For more information and latest versions, please visit:
[https://showtimemediacenter.com/](https://showtimemediacenter.com/)

## How to build for Linux

First you need to satisfy some dependencies:
For Ubuntu 12.04)  

	sudo apt-get install libfreetype6-dev libfontconfig1-dev libxext-dev libgl1-mesa-dev 
	libasound2-dev libasound2-dev libgtk2.0-dev libxss-dev libxxf86vm-dev libxv-dev libcdio-cdda-dev 
	libcddb2-dev libvdpau-dev yasm libpulse-dev libssl-dev curl
	libwebkitgtk-dev libsqlite3-dev

Then you need to configure:

	./configure

If your system lacks libwebkitgtk (Ubuntu 12.04 before 12.04.1) 
you can configure with

        ./configure --disable-webkit

If any dependencies are missing the configure script will complain.
You then have the option to disable that particular module/subsystem.

	make

Build the binary, after build the binary resides in `build.linux/`.
Thus, to start it, just type:

	build.linux/showtime

Settings are stored in `~/.hts/showtime`

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


## How to build for PS3 with PSL1GHT

$ ./Autobuild.sh -t ps3

## How to build for Raspberry Pi

$ ./Autobuild.sh -t rpi
