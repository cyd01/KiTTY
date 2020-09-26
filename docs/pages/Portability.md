<div style="text-align: center;"><iframe src="gad.html" frameborder="0" scrolling="no" style="border: 1px solid gray; padding: 0; overflow:hidden; scrolling: no; top:0; left: 0; width: 100%;" onload="this.style.height=(this.contentWindow.document.body.scrollHeight+5)+'px';"></iframe></div>
## Portability
By default, KiTTY uses the Windows registry database to save its configuration (sessions, host keys, parameters). It's possible to save it into a tree directories structure and to avoid writing anything into the registry.

To do that you just have to create a file called **kitty.ini** in the same directory where you put KiTTY binary, and add these two lines:
```
[KiTTY]
savemode=dir
```

At the very first use, it is possible to copy all the configuration from the registry for users who already created sessions with **normal** KiTTY mode. You just have to run the software on the command line with the parameter `-convert-dir`:
```
kitty.exe -convert-dir
```

This option will create 6 sub-directories: 

* Commands
* Folders
* Launcher
* Sessions
* Sessions_Commands
* SshHostKeys

containing all the configuration.
 
Unlike the registry (classic) mode, it is possible to have several saved sessions with the same name, but in different folders. So that, in order to start a session with command-line option (**-load**) it is necessary to specify the folder where the session file is located with the **-folder** option.
```
kitty.exe -folder SomeFolder/SomeSubFolder -load SessionName
```

  
By default any filename can contains session settings. It is possible to define a specific extention for these files:
```
[KiTTY]
fileextension=.ktx
```
