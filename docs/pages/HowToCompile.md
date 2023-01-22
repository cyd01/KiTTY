<div style="text-align: center;"><iframe src="gad.html" frameborder="0" scrolling="no" style="border: 1px solid gray; padding: 0; overflow:hidden; scrolling: no; top:0; left: 0; width: 100%;" onload="this.style.height=(this.contentWindow.document.body.scrollHeight+5)+'px';"></iframe></div>
## How to compile

The **KiTTY** binary is built with [MinGW32](http://mingw.org/).

The best way to compile **KiTTY** is to use our cross-compile docker image:

**For 32 bits compilation**
```bash
mkdir -p builds 2> /dev/null || rm -f builds/*.exe
docker run --rm -it -v $(pwd)/builds:/builds -e USR_UID=$(id -u) -e USR_GID=$(id -g) cyd01/cross-gcc "git clone https://github.com/cyd01/KiTTY.git ; cd KiTTY/0.76b_My_PuTTY/windows ; make -f MAKEFILE.MINGW cross ; cd /builds ; ls -l"
```

**For 64 bits compilation**
```bash
mkdir -p builds 2> /dev/null || rm -f builds/*.exe
docker run --rm -it -v $(pwd)/builds:/builds -e USR_UID=$(id -u) -e USR_GID=$(id -g) cyd01/cross-gcc "git clone https://github.com/cyd01/KiTTY.git ; cd KiTTY/0.76b_My_PuTTY/windows ; make -f MAKEFILE.MINGW cross64 ; cd /builds ; ls -l"
```

The **kitty.exe** and other stuff will be available into **/builds** directory.
