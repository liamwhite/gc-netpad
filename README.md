# GC-NetPad

GC-NetPad is a thing I hacked up after playing with Dolphin on my computer.
Using the keyboard to control is doable, certainly. But I want to use my
GameCube controller, and I don't feel like spending money on an adaptor for
my computer that I probably won't use very much.

Solution:
Write a server for the Wii that pushes controller data out to a client, then
have the client emulate a joystick for the computer.


This program is written only for Linux users, and not planned to be extended to other
platforms.

### Compiling

Enter client/, type <code>make</code> to compile.

Building the server assumes that you have a way to run the generated files, and that
you already have devkitPro and libogc available.

<pre>
$ cd gc-netpad/server
$ export DEVKITPPC=/path/to/devkitppc
$ export DEVKITPRO=/path/containing-libogc/
$ make
</pre>
If you have wiiload in your environment, type <code>make run</code> to wiiload the executable.


### Running

Run the generated DOL or ELF file on your Wii.
Find the the Wii's IP address (use <code>nslookup wii</code> if you don't know)

Run the client:

<pre>
user$ sudo ./client &lt;Wii IP&gt;
[sudo] password for user:
Net initialized
</pre>

There will now be a new input device available in programs such as Dolphin.

### Todo
<ul>
  <li>Support for multiple GC pads, possibly Wiimotes as well?</li>
  <li>Provide precompiled Wii homebrew package</li>
</ul>
