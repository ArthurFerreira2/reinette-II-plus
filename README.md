# reinette II plus

### reinette goes graphical !

![screenshots](screenshots.png)

After [reinette](https://github.com/ArthurFerreira2/reinette) (Apple 1 emulator) and [reinette II](https://github.com/ArthurFerreira2/reinette-II) (the text only Apple II emulator), I am proud to release reinette II plus, a french\* Apple II plus emulator using SDL2.

\* reinette has two meanings in french : it's a little frog but also a delicious kind of apple

### Featuring :

* all video modes in color
* mono sound with mute/unmute
* 48KB (no language card support)
* paddles/joystic with trim ajustment
* paste text from clipboard
* disk ][ adapter with two drives (.nib files only)
* drag and drop nib files to inset a floppy
* save floppy changes back to host
* zoom the window by integer increments


It uses the same MOS 6502 CPU emulator as her sisters (now christened puce6502).\
You only need SDL2 to compile it. (I'm not using SDL_Mixer, but only the native SDL2 audio functions)


This emulator is not accurate in many ways and does not compete with
[AppleWin](https://github.com/AppleWin/AppleWin), [Epple](https://github.com/cmosher01/Epple-II) or [LinApple](https://github.com/linappleii/linapple). Better use one of them if you want a better Apple ][ experience.

I wrote it in the goal to better understand the Apple ][ internals, and I'm publishing the sources in the hope they will be of any help.

It's compact, less 1000 SLOC, with two source files only, one for the CPU emulation, the other for the computer itself.

I did my best to comment everything, and if you have an idea of how an Apple ][ works, it should be easy for you to understand the code, modify and enhance it for your needs (see TODO section).


### Startup

  You can specify a .nib file on the command line to start the emulator with a floppy engaged in drive 1. Otherwhise, the emulator will start with no floppy (and thus wait one until you press the reset key or drag and drop a nib file)


### Usage

Drag and drop a disk image file (.nib format only) to inset it into drive 1\
Drop the file while pressing the ALT key to insert it into drive 2

Use the functions keys to control the emulator itself

* F1    : writes the changes of the floppy in current drive back to host
* F5    : zoom out down to 1:1
* F6    : zoom in, no magnification limit
* F7/F8 : adjust joystic trim
* F9    : mute/unmute soud
* F10   : reset
* F11   : break
* F12   : quit (you can also just close the window)

Paddles / Joystic :

* numpad 1 : left
* numpad 3 : right
* numpad 2 : down
* numpad 3 : up 
* CTRL     : button 0
* ALT      : button 1


### limitations

* High pitch noise at high volume (might be related with my environment)
* Sound cracks when playing for long period (intro music for example)
* CPU is not 100% cycle accurate - see source file for more details
* Colors are approximate (taken from a scan of an old Beagle bros. poster)
* HGR is inhacurate, and does not implement color clashing
* disk ][ access is artificially accelerated
* Only support .nib floppy images. (you can use [CiderPress](https://github.com/fadden/ciderpress) to convert your images to this format)
* only has 48KB of RAM (can't run some software requiring the language card)
* and many others ...


### To do :

* warn if you quit the application with unsaved floppy changes
* give the user the option to switch to the original Apple II rom
* colors where taken from an old Beagle Bros poster, find more accurate RGB values.
* implement color clashing in HGR
* optimize sound generation
* optimize disk access (actually a bit artificial)
* re-implement Paddles and Joystic support for a better analog simulation
* implement a language card to extend the RAM of reinette II plus to 64K and support more sofware.


*simplicity is the ultimate sophistication*
