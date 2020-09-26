<div style="text-align: center;"><iframe src="gad.html" frameborder="0" scrolling="no" style="border: 1px solid gray; padding: 0; overflow:hidden; scrolling: no; top:0; left: 0; width: 100%;" onload="this.style.height=(this.contentWindow.document.body.scrollHeight+5)+'px';"></iframe></div>
## Menu shortcuts definition

It is possible to associate a **keyboard shortcut** for each item of the main menu.  
Just complete the **[Shortcuts]** section of the **kitty.ini** configuration file.

Here are all the possible values:

```
# Shortcuts: definition for the menu shortcuts keys
[Shortcuts]
; (re)send automatic command (default is SHIFT+F12)
autocommand=
; Change settings ...
changesettings=
; Clear scrollback
clearscrollback=
; Close and restart current session
closerestart=
; run a local command (default is CONTROL+F5)
command=
; Copy all window buffer to clipboard
copyall=
; Open a duplicate window (with same session settings)
duplicate=
; open text editor connected to the main window (default is SHIFT+F2)
editor=
; open text editor with clipboard content, connected to the main window (default is CONTROL+SHIFT+F2)
editorclipboard=
; Show event log
eventlog=
; Switch font to black on white colors
fontblackandwhite=
; Decrease font size
fontdown=
; Switch font to negative colors
fontnegative=
; Increase fonr size
fontup=
; Switch to full screen
fullscreen=
; receive a remote file with pscp.exe: the full path must be selected in clipboard (default is CONTROL+F4)
getfile=
; change the background image (default is CONTROL+F11)
imagechange=
; special command box (default is CONTROL+F8)
input=
; special command with multi-line editor (default is SHIFT+F8)
inputm=
; Repeat key exchange
keyexchange=
; New session ...
opennew=
; Print current clipboard content (default if SHIFT+F7)
print=
; Print all window buffer content (default is F7)
printall=
; Protect the window, disable keyboard and mouse input (default is CONTROL+F9)
protect=
; Reset terminal
resetterminal=
; Roll-up the window into the title bar (default is CONTROL+F12)
rollup=
; Load a local script and run it remotely (default is CONTROL+F2)
script=
; Send a local file with pscp.exe (default is CONTROL+F3)
sendfile=
; Show current port forwarding definition (default is SHIFT+F6)
showportforward=
; Enable or disable logging (default is SHIFT+F5)
switchlogmode=
; Send the window to the system tray (default is CONTROL+F6)
tray=
; Switch to embedded image viewer (default SHIFT+F11)
viewer=
; Switch to always visible (default is CONTROL+F7)
visible=
; Start WinSCP (default is SHIFT+F3)
winscp=
```
