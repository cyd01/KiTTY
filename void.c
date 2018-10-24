#ifdef PERSOPORT
//void void_function(void) {}
#endif

/*

gcc   -mno-cygwin -Wall -O2 -D_WINDOWS -DWIN32S_COMPAT -DFDJ1 -DPERSOPORT -DCYGTERMPORT -DIMAGEPORT -DRECONNECTPORT -DHYPERLINKPORT -DZMODEMPORT -DSTARTBUTTON -D_NO_OLDNAMES -DNO_MULTIMON -DNO_HTMLHELP -I.././ -I../charset/ -I../windows/ -I../unix/ -I../mac/ -I../macosx -I../../sc -I../../url -D_WIN32_IE=0x0500 -DWINVER=0x0500 -D_WIN32_WINDOWS=0x0410 -D_WIN32_WINNT=0x0500 -c ../../void.c -I../../
cp void.o cygcfg.o
cp void.o cygterm.o
cp void.o winpzmodem.o
cp void.o kitty_image.o

*/
