# Welcome to the KiTTY introduction web site

<p style="text-align: center;">
All KiTTY documentation is available on the official website<br/>
https://www.9bis.com/kitty/
</p>

## What is KiTTY ?
KiTTY is a fork from version 0.76 of **PuTTY**, the best telnet / SSH client in the world.
KiTTY is only designed for the Microsoft(c) Windows(c) platform. For more information about the original software, or pre-compiled binaries on other systems, you can go to the [Simon Tatham PuTTY page](http://www.chiark.greenend.org.uk/~sgtatham/putty/ "PuTTY").

KiTTY has all the features from the original software, and adds many others as described below:

### The very first requested features
* Sessions filter
* Portability
* Shortcuts for pre-defined command
* The session launcher
* Automatic logon script
* Automatic logon script with the RuTTY patch
* URL hyperlinks

### Technical features
* Automatic password
* Automatic command
* Running a locally saved script on a remote session

### Graphical features
* An icon for each session
* Send to tray
* Transparency
* Protection against unfortunate keyboard input
* Roll-up
* Always visible
* Quick start of a duplicate session
* Enhanced Configuration Box

### Other features
* Automatic saving
* SSH Handler: Internet Explorer integration
* pscp.exe and WinSCP integration
* Binary compression
* Clipboard printing
* Cygwin and cmd.exe integration
* File association
* Other settings
* New command-line options

### Bonus
* A light chat server is hidden in KiTTY
* A hidden text editor is integrated into KiTTY

## Official download page

KiTTY is available at our main CDN: [Fosshub](https://www.fosshub.com/KiTTY.html).

## How to compile
Inside your MSYS & MinGW32 environment jump into the x.yy_My_PuTTY\windows directory then run the command:

    make -f MAKEFILE.MINGW putty.exe

### How to install the MSYS/MinGW32 environment
Simple guide to setup the compile environment:

- Download the `mingw-builds-install.exe` from https://sourceforge.net/projects/mingwbuilds/files/host-windows/releases/4.8.0/32-bit/
- Execute the installer and select the **GUI**.
- After installing it, only the "packager manager" it's in fact installed, so execute `$MinGW\libexec\mingw-get\guimain.exe` and select in the Basic Setup the packages: **"mingwg-developer-tools", "mingw32-base", "mingw32-gcc-g++" and "msys-base"**.
- When all is installed, to open the SHELL execute `$MinGW\msys\1.0\msys.bat`. Then from from this shell you can compile. Example: `cd $KiTTY_DIR/0.74_My_PuTTY/windows` and `make -f MAKEFILE.MINGW 9bis`.

Original website is [https://www.9bis.com/kitty/](https://www.9bis.com/kitty/ "KiTTY website").
