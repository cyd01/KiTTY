<div style="text-align: center;"><iframe src="gad.html" frameborder="0" scrolling="no" style="border: 1px solid gray; padding: 0; overflow:hidden; scrolling: no; top:0; left: 0; width: 100%;" onload="this.style.height=(this.contentWindow.document.body.scrollHeight+5)+'px';"></iframe></div>
## New command-line options

<!-- CmdLineOptions_begin -->

In **KiTTY** some new command-line options are available:

* **-auto-store-sshkey**: automatically store server SSH key without prompting
* **-classname**: to define a specific class name for the window
* **-cmd**: to define a startup auto-command
* **-codepage**: to select a new remote character set (use in combination with [-localproxy](cygtermd.md) option)
* **-convert-dir**: convert registry settings to [savemode=dir mode](Portability.md)
* **-defini**: create a default configuration kitty.template.ini
* **-edit**: edit the settings of a session
* **-fileassoc**: associate **.ktx** files with **KiTTY**. See [Portability feature](Portability.md) to define file extention
* **-folder**: directly open a specific folder (for [savemode=dir mode](Portability.md) only). It must precede **-load** option
* **-fullscreen**: start directly in full screen mode
* **-help**: print this help message
* **-icon**: choose a specific [build-in icon](kitty_icon.md)
* **-iconfile**: choose an external icon file
* **-initdelay**: delay (in seconds) before initial configured actions (send to tray, autocommand ...). Default is 2.0
* **-keygen**: start the integrated ssh key generator
* **-knock**: set port knocking sequence
* **-kload**: load a **.ktx** file (that contains session settings)
* **-launcher**: start [the session launcher](SessionLauncher.md)
* **-localproxy**: define a local proxy for new [Cygterm](cygtermd.md) feature
* **-log**: create a log file
* **-loginscript**: load a login script file
* **-nobgimage**: to disable background image feature
* **-noctrltab**: disable **CTRL+TAB** feature
* **-nofiles**: disable the creation of default ini file if it does not exist
* **-noicon**: disable [icons](ThatsAllFolks.md) support
* **-noshortcuts**: disable all shortcuts
* **-notrans**: disable [Transparency](Transparency.md) support
* **-nozmodem**: disable [ZModem](ZModem.md) support
* **-pass**: set a password
* **-putty**: disable with one option all **KiTTY** new features
* **-runagent**: start the integrated SSH agent
* **-send-to-tray**: start a session directly in the [system tray](SendToTray.md) (useful for SSH tunnels)
* **-sendcmd**: to send a command to all windows with the same class name
* **-sshhandler**: create protocols associations (**telnet://**, **ssh://**) for internet explorer
* **-title**: set a window title
* **-version**: only open the about box
* **-xpos**: to set the initial X position
* **-ypos**: to set the initial Y position

In **Klink** there is one of the **KiTTY** option:

* **-auto-store-sshkey**: automatically store server SSH key without prompting

In **Kageant** there is only one:

* **-pass**: to set the passphrase of the ssh key to add

<!-- CmdLineOptions_end -->

> All original **PuTTY** command-line options are available on [PuTTY website](https://the.earth.li/~sgtatham/putty/latest/htmldoc/Chapter3.html#using-cmdline)
