<div style="text-align: center;"><iframe src="gad.html" frameborder="0" scrolling="no" style="border: 1px solid gray; padding: 0; overflow:hidden; scrolling: no; top:0; left: 0; width: 100%;" onload="this.style.height=(this.contentWindow.document.body.scrollHeight+5)+'px';"></iframe></div>
## SSH Handler: Internet Explorer integration

Now it is possible to use **KiTTY** with Internet Explorer.
**KiTTY** can be integrated into Internet Explorer (or any other browser, such as Firefox) and so become the program linked with **putty://**, **telnet://** or **ssh://** links.

First you have to download the file [kitty_ssh_handler.reg](../files/kitty_ssh_handler.reg).
Then you must correct it to write the full path to the file kitty.exe on your system.
Finally, run it to update the registry.

Alternatively you can also update the registry with the command-line parameter `kitty.exe -sshhandler`.

After this update it is possible to create HTML pages with **ssh://** and **telnet://** links.

Try this example: 

    <a href="telnet://towel.blinkenlights.nl">telnet://towel.blinkenlights.nl</a>
