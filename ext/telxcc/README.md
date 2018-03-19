# telxcc
[![Build Status](https://travis-ci.org/petrkutalek/telxcc.svg?branch=master)](https://travis-ci.org/petrkutalek/telxcc)

**NOTE: telxcc is very likely no longer maintained. It is licensed under GPL: anyone can use it for free, anyone has the right to modify it if she does not use the name "telxcc" for further releases. Please rename your clone after forking.**


telxcc is utility extracting teletext Closed Captions from Transport Stream binary files (TS & M2TS) into SubRip text files (SRT).

telxcc is:

* tiny and lightweight (few KiBs binary, no lib dependencies)
* easy to use
* open-source
* multiplatform (Mac, Windows and Linux @ x86, ARM etc.)
* modern (fully supports UTF-8 (Unicode Normalization Form C (NFC)), colours in SRT files, conforms to ETSI 300 706 Presentation Level 1.5/2.5, able to process TS and M2TS)
* stable
* secure (does not require any escalated privileges)
* high performing (even SSD is the bottleneck)
* well tested (every build is automatically tested against English, German, Czech, Italian, Norwegian, Swedish, Finnish, Slovenian and Polish TS files from different TV stations)
* 100% handcrafted in Prague, CZ. :)


telxcc is easy to use and flexible at the same time:

* telxcc could be run in "search engine mode", in which case it produces plain text output suitable for indexing (UTC airtime and caption in plain text)
* telxcc tries to automatically detect all parameters needed (transport stream ID, teletext CC page, timestamps) and environment (the way it is started on Windows for instance)
* it could be easily integrated (files could be redirected or specified on command line as parameters)


telxcc is *the only utility having correct implementation* for following languages

* Croatian
* Czech
* English
* Estonian
* Finnish
* French
* German
* Hungarian
* Italian
* Lettish
* Lithuanian
* Polish
* Portuguese
* Rumanian
* Serbian
* Slovak
* Slovenian
* Spanish
* Swedish
* Turkish


telxcc also has limited/untested implementation of cyrillic and Greek alphabet for

* Bulgarian
* Croatian
* Greek
* Russian
* Serbian
* Ukrainian

and it is also already prepared for arabic and hebrew scripts (no such TS samples are unfortunately available for testing).


Important: telxcc will *never ever* be like Emacs; it is simple and specialized utility. Do you need another output format? Just transform current one. Is online/realtime processing over TCP/IP required? I suggest Node.js as a wrapper…

**Unfortunately I am unable to provide you with free support. Please, do not ever ask me to assist you with source code modifications or to make a special build for you etc., if you use telxcc for your business (especially if you have not donated to its development). It is your job you are paid for.**


## 3rd party software known to be using telxcc

* [QtlMovie](http://qtlmovie.sourceforge.net) by Thierry Lelegard
* [CCExtractor](http://ccextractor.sourceforge.net) by Carlos Fernandez
* [Hybrid](http://www.selur.de) by Georg Pelz


## Binaries

For precompiled binary files see [Releases](https://github.com/petrkutalek/telxcc/releases) page.


## Build

To install, or uninstall telxcc on Linux and Mac:

    $ make install ↵

    $ make uninstall ↵

To build binary for Intel Core 2 processor architecture just type:

    $ make ↵

On Mac typically you can use clang preprocessor:

    $ make CC=clang ↵

You can also copy any \*.ts files into the current directory and build a profiled version (up to 3% performance gain on repeat tasks):

    $ make profiled ↵

Or you can disable all optimizations (binary target is any x86 processor):

    $ make CCFLAGS="-Wall -pedantic -std=gnu99"

Windows binary is build in MinGW by (MinGW must be included in PATH):

    C:\devel\telxcc> mingw32-make -f Makefile.win strip

telxcc has no lib dependencies and is easy to build and run on Linux, Mac and Windows. (Generic binary files are included.)


## Command line params

    $ ./telxcc -h ↵
    telxcc - TELeteXt Closed Captions decoder
    (c) Forers, s. r. o., <info@forers.com>, 2011-2014; Licensed under the GPL.
    Version 2.5.3 (Apple), Built on Jan 16 2014
    
    Usage: telxcc -h
      or   telxcc -V
      or   telxcc [-v] [-m] [-i INPUT] [-o OUTPUT] [-p PAGE] [-t TID] [-f OFFSET] [-n] [-1] [-c] [-s [REF]]
    
      -h          this help text
      -V          print out version and quit
      -v          be verbose
      -m          input file format is BDAV MPEG-2 Transport Stream (BluRay and some IP-TV recorders)
      -i INPUT    transport stream (- = STDIN, default STDIN)
      -o OUTPUT   subtitles in SubRip SRT file format (UTF-8 encoded, NFC) (- = STDOUT, default STDOUT)
      -p PAGE     teletext page number carrying closed captions
      -t TID      transport stream PID of teletext data sub-stream
                  if the value of 8192 is specified, the first suitable stream will be used
      -f OFFSET   subtitles offset in seconds
      -n          do not print UTF-8 BOM characters to the file
      -1          produce at least one (dummy) frame
      -c          output colour information in font HTML tags
      -s [REF]    search engine mode; produce absolute timestamps in UTC and output data in one line
                  if REF (unix timestamp) is omitted, use current system time,
                  telxcc will automatically switch to transport stream UTC timestamps when available
    

    $ man ./telxcc.1 ↵

    
## Usage example

    $ ./telxcc < TVP.ts > TVP.srt ↵
    telxcc - TELeteXt Closed Captions decoder
    (c) Forers, s. r. o., <info@forers.com>, 2011-2014; Licensed under the GPL.
    Version 2.5.3 (Apple), Built on Jan 16 2014
    
    - Found VBI/teletext stream ID 205 (0xcd) for SID 45 (0x2d)
    - PID 0xbd PTS available
    - Programme Identification Data = OGLOSZENIA -> 640
    - Programme Timestamp (UTC) = Thu Mar 28 14:40:00 2013
    - Transmission mode = serial
    - Using G0 Latin National Subset ID 0x1.0 (Polish)
    ! Unrecoverable data error; UNHAM8/4(70)
    - There were some CC data carried via pages = 777 778 484 488 
    - Done (181250 teletext packets processed, 321 frames produced)

    $ _


## Usage on Windows

Drag and drop your .TS file onto the batch file extract-subtitles.cmd. A .SRT file with the same name as your video file should be created. (Thanks a lot for this hint Jeremy!)
