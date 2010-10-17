PSGroove
========

This is the PSGroove, an open-source reimplementation of the psjailbreak exploit for
non-usb ATMega chips. (Like those used in arduinos.)

It should work on:

- ATMega1280
- ATMega168
- ATMega328p

... and maybe more.

**This software is not intended to enable piracy, and such features
have been disabled.  This software is intended to allow the execution
of unsigned third-party apps and games on the PS3.**

Cloning
-------
    git clone git://github.com/psgroove/psgroove.git for original
    git clone git://github.com/ixhundred/psgroove.git for timwu-ixhundred version (for atmel avr that not support usb h/w)


Configuring
-----------
Chip and board selection can usually be handled in the Makefile.
In particular, update the MCU, BOARD, and F_CPU lines.  Suggested values:

Arduino Mega

    MCU = atmega1280
    BOARD = ArduinoMega
    F_CLOCK = 16000000

Arduino Duemilanove

    MCU = atmega168
    BOARD = ArduinoDuemilanove (default for ixhundred version, see the h/w circuit from timwu)
    F_CLOCK = 16000000

Board-specific notes
--------------------
See README in subfolder for Boards corresponding to your particular board.

Building
--------
On Linux, use the AVR GCC toolchain (Debian/Ubuntu package: gcc-avr).
On Windows, WinAVR should do the trick.

    make clean
    make


Programming
-----------

To program, just edit the programming options section of the Makefile
to match your particular board programming setup. You will need avrdude
on your path. Then do:

    make program
    
For arduino users, you can get some hints as to how to configure the
programming options by checking what the arduino IDE does.

1. Edit your arduino preferences file setting upload.verbose to true.
2. Open the arduino ide
3. Create an empty sketch
4. Upload to your board, and check the output.

From the IDE's output you should be able to determine what port to use
and other useful information about configuring the options.

Using
-----
To use this exploit:
  
* Hard power cycle your PS3 (using the switch in back, or unplug it)
* Plug the dongle into your PS3.
* Press the PS3 power button, followed quickly by the eject button.

After a few seconds, the first LED on your dongle should light up.
After about 5 seconds, the second LED will light up (or the LED will
just go off, if you only have one).  This means the exploit worked!
You can see the new "Install Package Files" menu option in the game
menu.


Notes
-----
A programmed dongle won't enumerate properly on a PC, so don't worry
about that.

This branch has a modified payload that adds peek and poke syscalls 
to the lv2 kernel. A userspace application can use these syscalls to 
dump out the entire memory space of the kernel, or patch the kernel
as it is running.  

Unfortunately, because the free toolchain/sdk is not ready, we can't
distribute an application to do the dumping, so you will have to make
your own.

The lv2 kernel starts at 0x8000000000000000

Peek
----
 * Syscall 6.
 * r3 is a 64 bit address to read
 * A 64 bit value will be returned in r3

Poke
----
 * Syscall 7.
 * r4 is a 64 bit value
 * r3 is the address to write that value to
