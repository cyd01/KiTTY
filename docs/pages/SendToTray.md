<div style="text-align: center;"><iframe src="gad.html" frameborder="0" scrolling="no" style="border: 1px solid gray; padding: 0; overflow:hidden; scrolling: no; top:0; left: 0; width: 100%;" onload="this.style.height=(this.contentWindow.document.body.scrollHeight+5)+'px';"></iframe></div>
## Send to tray

When you use KiTTY to run long background batches, or when KiTTY is just used to open SSH tunnels (with or without remote shell), you can move the window to the Windows system tray (the zone in the bottom-right corner of the screen). There are several ways to do this.

----

### To create a session that runs directly in the system tray on startup

To create a session that starts directly in the system tray select the **Send to tray on startup** option in the **Window/Behaviour** panel of the configuration box.

![](../img/config_sendtotray.jpg)

----

### To move an open session into the system tray

To move a session that is already open into the system tray:
 
* select **Send to tray** item from main menu
* or press **CTRL+F6** two keys
* or press **CTRL** key, then click on **left mouse button** and point then **Minimize** button of the window

----
 
### To run from the command-line
 When running from the command-line, you can start a session directly in the system tray. Just use the option **-send-to-tray**.
 
