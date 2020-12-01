
<div style="text-align: center;"><iframe src="gad.html" frameborder="0" scrolling="no" style="border: 1px solid gray; padding: 0; overflow:hidden; scrolling: no; top:0; left: 0; width: 100%;" onload="this.style.height=(this.contentWindow.document.body.scrollHeight+5)+'px';"></iframe></div>
## About cygtermd, the PuTTYCyg workaround

In order to run **Cygwin** terminal or **cmd.exe** directly embedded into a KiTTY window, previously there was a specific feature: **PuTTYCyg patch**. The patch was broken with the **[0.71](0.71.md)** version.  
PuTTY team has proposed a workaround for PuTTYCyg patch. Read the [cygwin-terminal-window](https://www.chiark.greenend.org.uk/~sgtatham/putty/wishlist/cygwin-terminal-window.html) page.

The pre-compiled version of **cygtermd.exe** is available [here](../files/cygtermd.zip).
Download it and put it into your **/bin** Cygwin directory.
  
  
### Start a **Cygwin environment** into a **KiTTY** terminal

This feature is also available directly from the command-line. Once the **cygtermd.exe** is put into the **Cygwin** directory just call this command:
```
kitty.exe -localproxy "C:\cygwin64\bin\cygtermd.exe /home/%USERNAME% /bin/bash -login" localhost
```

Adapt **C:\cygwin64\bin\cygtermd.exe** if it has been installed to a different directory. You must provide a target hostname (ex: localhost) even if it is not used in this situation.

If you are running this command from PowerShell rather than a Windows shortcut, Start➡Run, or a regular command prompt, you should use single quotes instead of double quotes.
```
kitty.exe -localproxy 'C:\cygwin64\bin\cygtermd.exe /home/%USERNAME% /bin/bash -login' localhost
```
 
### Start a **CMD.EXE** into a **KiTTY** terminal

It is possible to start directly the Windows command line utility **cmd.exe**:
```
kitty.exe -localproxy "C:\cygwin64\bin\cygtermd.exe /cygdrive/c/Windows /cygdrive/c/Windows/System32/cmd.exe" localhost
```
Don't forget to choose the right codepage in translation tab or use the **-codepage** command line option.

It is not necessary to have the full **Cygwin** environment to do this. Just add the **cygtermd.exe** file and the corresponding **cygwin1.dll** library next the **kitty.exe** binary.
  
 
### Use **winpty** to start **cmd.exe** or ** PowerShell** Windows utilities

It is also possible to start the Windows command line utility **cmd.exe** from the [winpty tool](https://github.com/rprichard/winpty):
Get it and unzip it into your **/usr** Cygwin directory.
Then run
```
kitty.exe -localproxy "C:\cygwin64\bin\cygtermd.exe /usr/bin/winpty.exe C:\Windows\System32\cmd.exe" localhost
```

And now use it to run **PowerShell**:
```
kitty.exe -localproxy "C:\cygwin64\bin\cygtermd.exe /usr/bin/winpty.exe C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe" localhost
```

or **PowerShell 7**:
```
kitty.exe -localproxy "C:\cygwin64\bin\cygtermd.exe /usr/bin/winpty.exe C:\Program Files\PowerShell\7\pwsh.exe" localhost
```

---

If the provided **cytermd.exe** does not work in your environment, please follow these instructions to build your own binary:

1. Install your own Cygwin, with packages make, git and gcc
1. Open the Cygwin shell
1. Run the following commands

```
mkdir putty
cd putty
git clone https://git.tartarus.org/simon/putty.git
cd putty/contrib/cygtermd
make
cp cygtermd.exe /usr/bin/
```

*Many thanks to [lars18th](https://github.com/lars18th) for the help*
