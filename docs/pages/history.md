Waou so much passion !
First let's share some history.

I've started to work on PuTTY in **2003** with the version **0.53** of the project. At that time, no source management solution (no git, svn, cvs or any other). Remember I was not a developer. I was an ops that need a software to connect servers through telnet (yes!). I liked PuTTY but I though features were missing. My first need was to automate the password negotiation on solaris or hp-ux servers: no Linux one ... and no GUI. The second need was to be able to define a specific icon for each terminal window ... the story has begun. That was the creation of **MOD_PERSO** patch (called **PERSOPORT** at this time).
My fork was build directly on my desktop computer (under Windows and GCC) and my work remains into a directory on my professional machine, nothing else.

Each time a new PuTTY version was released I worked to merge it with my modifications.
In **2006** I've started to share my work. That was the creation of the [KiTTY web site](https://www.9bis.net/kitty) with the version **0.58**. After that version, people that found KiTTY useful began to ask for integration with other PuTTY forks (LePuTTY, PuTTYTray, TuTTY, RuTTY ...). Here began **MOD_*** patches. The KiTTY source files were now downloadable from the web site: a simple **.tar.gz** file that is always available [here](https://www.9bis.net/kitty/files/kitty_src.tar.gz).
KiTTY became more and more popular and many requests were integrated.

In **2010** I've started to use a version control system to be able to trace all the modifications I've made. In my company we used **subversion** and the very first versioned KiTTY version was **0.60.66.30**.

Late in **2013** the **0.63** PuTTY version was released. It was a terrible big bang. The internal session settings structure was completely rewritten. It was a huge work to accomplish the merge with KiTTY. I've decided that I need help to maintain KiTTY in the future. Few months after, I've decided to share not only the source code, but the version control system too. That was the beginning of **svn** on my [web site](https://ken.9bis.com/public/websvn/listing.php?repname=Sandbox&) in **2015**.

Finally in **2018** I've switch to **git** with version **0.70**, and the [KiTTY Gihub repository](https://github.com/cyd01/KiTTY) opened in october the same year. 

Version **0.71** in **2019** was another revolution. Some of the MOD_* patches, I did not write (remember I've merge them), became broken by the last PuTTY release and I had to sacrifice some of them (cygterm, background image ...). I've asked for some help, and decided to switch completely to English (web site, comment, release notes ...) in early **2019**. At mid **2019** I abandoned the old multi-language version of the web site to the current English-only one, based on markdown files.

The **0.77** in **mid 2022** was a little revolution too: PuTTY team decided to completely reorganize the source files structure. Many files were divided into several sub-directories. It was a good thing: the source code now is mush more easy to read. But, another time, the work to adapt KiTTY was huge.

Now, why not to make KiTTY compatible with Linux. First because in 2003 I simply did not think of it. Second, like I wrote, I'm not the author of all KiTTY patches, and I don't know if they are all compatible. And last, I am also a KiTTY user and I use KiTTY on Windows machine only (remember it is the only platform that does not have a ssh client in standard). When I am on a Linux GUI, I simply use **ssh**  to jump to other machine. So I won't work on the Linux compatibility: I don't even know how to achieve it, and I really don't have time to make it. KiTTY is not my real job even if I really like to work on it.

Anyway I will try to add all the comments people need to understand my modifications. I agree with you @lars18th, I could add some generic informations to **kitty.h** file. 

And above all, I've never refused to merge interesting patch in KiTTY ... but the fact is that along these 13 years I don't receive much help. I think people often mix open source with free software. Anyway feel free to propose such a patch.

All the best
Cyd
