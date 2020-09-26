<div style="text-align: center;"><iframe src="gad.html" frameborder="0" scrolling="no" style="border: 1px solid gray; padding: 0; overflow:hidden; scrolling: no; top:0; left: 0; width: 100%;" onload="this.style.height=(this.contentWindow.document.body.scrollHeight+5)+'px';"></iframe></div>
## The PuTTYCyg patch

If you are interested in [PuTTYCyg](http://code.google.com/p/puttycyg/) features, we have ported them into the **KiTTY** software.

> This feature is broken from **0.71** version.
But PuTTY team has proposed a [workaround](cygtermd.md).

![](../img/puttycyg.ico)
To activate these features, download and unzip the **PuTTYCyg patch** into the same directory where you put KiTTY:

- [PuTTYCyg patch for Cygwin 1.5](../files/KiTTYCyg.zip)
- [PuTTYCyg patch for Cygwin 1.7](../files/KiTTYCyg.1.7.zip)
- [PuTTYCyd patch for Cygwin 64bits 1.7](../files/KiTTYCyg64.1.7.zip)
- [PuTTYCyd patch for Cygwin 64bits 2009.0.0.0](../files/KiTTYCyg64-2009.0.0.0.zip)
 
Then add these two li
nes to your **%APPDATA%/KiTTY/kitty.ini** configuration file:

    [KiTTY]
    cygterm=yes

A new option **CtHelperPath** is added to point the complete path to the **cthelper.exe** file (which now can be included into the Cygwin binary directory: **C:\Cygwin\bin**).
In 64 bits mode, don't forget to check this option in **Connection/Cygwin** panel.
 
