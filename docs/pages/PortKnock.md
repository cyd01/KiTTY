<div style="text-align: center;"><iframe src="gad.html" frameborder="0" scrolling="no" style="border: 1px solid gray; padding: 0; overflow:hidden; scrolling: no; top:0; left: 0; width: 100%;" onload="this.style.height=(this.contentWindow.document.body.scrollHeight+5)+'px';"></iframe></div>
## Port knocking

Sometimes it can be useful to protect the **ssh port** of a server from attack, especially if the server is available on internet.
[Port knocking](http://yalis.fr/cms/index.php/post/2016/06/07/Light-weight-port-knocking-to-protect-SSH) is one of the well known method.

**KiTTY** can handle with port knocking sequence. Define you sequence in **Connection** tab of the configuration box.

![](../img/config_connection.jpg)

The sequence is a coma separated list of ***port***:***protocol***.
