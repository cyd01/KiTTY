<div style="text-align: center;"><iframe src="gad.html" frameborder="0" scrolling="no" style="border: 1px solid gray; padding: 0; overflow:hidden; scrolling: no; top:0; left: 0; width: 100%;" onload="this.style.height=(this.contentWindow.document.body.scrollHeight+5)+'px';"></iframe></div>
## Configuration box

The saved sessions configuration box on **PuTTY** is very small. Only **6 sessions** can be seen at a time. By default, in **KiTTY** this dialog box is increased to **21 lines**. This height is resizable.

To adjust the number of lines in the saved sessions configuration box, add these two lines to the **kitty.ini** configuration file:

    [ConfigBox]
    height=21
 
To  disable the automatic sessions name filter (that is the default behaviour) add this setting:

    [ConfigBox]
    filter=no

To modify the size of the configuration box window (for non standard dpi settings) just add this:

    [ConfigBox]
    windowheight=800
