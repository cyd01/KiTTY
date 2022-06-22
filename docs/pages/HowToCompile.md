<div style="text-align: center;"><iframe src="gad.html" frameborder="0" scrolling="no" style="border: 1px solid gray; padding: 0; overflow:hidden; scrolling: no; top:0; left: 0; width: 100%;" onload="this.style.height=(this.contentWindow.document.body.scrollHeight+5)+'px';"></iframe></div>
## How to compile

The **KiTTY** binary is built with [MinGW32](http://mingw.org/).

In order to compile **KiTTY**, on your Windows PC:

  * get the source code from the SVN or Git repository (see Download page)
  * start the MinGW environment
  * dig into the Windows directory
  * run the command `make -f MAKEFILE.MINGW putty.exe`

On **Linux** machine it is also possible to cross compile. Example with our cross-compile docker image:

**For 32 bits compilation**
```bash
mkdir builds 2> /dev/null || rm -f builds/*.exe
docker run --rm -it -v $(pwd)/builds:/builds cyd01/cross-gcc "git clone https://github.com/cyd01/KiTTY.git ; cd KiTTY/0.76_My_PuTTY/windows ; make -f MAKEFILE.MINGW cross ; cd /builds ; ls -l"
```

**For 64 bits compilation**
```bash
mkdir builds 2> /dev/null || rm -f builds/*.exe
docker run --rm -it -v $(pwd)/builds:/builds cyd01/cross-gcc "git clone https://github.com/cyd01/KiTTY.git ; cd KiTTY/0.76_My_PuTTY/windows ; make -f MAKEFILE.MINGW cross64 ; cd /builds ; ls -l"
```

The **kitty.exe** and other stuff will be available into **/builds** directory.
