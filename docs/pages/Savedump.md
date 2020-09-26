<div style="text-align: center;"><iframe src="gad.html" frameborder="0" scrolling="no" style="border: 1px solid gray; padding: 0; overflow:hidden; scrolling: no; top:0; left: 0; width: 100%;" onload="this.style.height=(this.contentWindow.document.body.scrollHeight+5)+'px';"></iframe></div>
## How to make a memory dump

In order to help bug analysis, it's possible to make a memory dump of KiTTY. 

* Get the very last beta build: https://www.9bis.net/kitty/files/kitty-debug.exe
* Add **debug=yes** option to the **[KiTTY]** section of **kitty.ini** file
* Start the session that makes the issue
* Press **CTRL+F8** keys
* Write **/savedump** into the input box and press Enter
* A **kitty.dmp** file is created into the same directory where **kitty.exe** is located
* Remove **debug=yes** option 
