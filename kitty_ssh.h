#ifndef KITTY_SSH
#define KITTY_SSH

#ifdef PORTKNOCKINGPORT
#define PROTO_TCP 1
#define PROTO_UDP 2
int knock( char *hostname, unsigned short port, unsigned short proto) ;
int ManagePortKnocking( char* host, char *portstr ) ;
#endif

#endif
