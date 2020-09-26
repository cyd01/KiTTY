<div style="text-align: center;"><iframe src="gad.html" frameborder="0" scrolling="no" style="border: 1px solid gray; padding: 0; overflow:hidden; scrolling: no; top:0; left: 0; width: 100%;" onload="this.style.height=(this.contentWindow.document.body.scrollHeight+5)+'px';"></iframe></div>
## Automatic saving

**KiTTY** uses the Windows registry to save all its configuration (sessions, host keys, parameters). The key is **[HKEY_CURRENT_USER\Software\9bis.com\KiTTY]**.

For safety reasons, this key is always saved each time the configuration is modified (when you quit the configuration dialog box).

The file name is **kitty.sav**. This file is located in the same place as **kitty.ini**: the directory **%APPDATA%\KiTTY**.

When using KiTTY for the first time, both registry key and configuration are empty. So that the software will copy all sessions defined in the PuTTY registry key if there are any.

----

It is possible to define another place for saved file. Add these lines to the **%APPDATA%\KiTTY\kitty.ini** configuration file.

```
[KiTTY]
sav=C:\Temp\KiTTY\kitty.sav
```
