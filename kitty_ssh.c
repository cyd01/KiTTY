#include "kitty_ssh.h"
#include "kitty_tools.h"

#ifdef PORTKNOCKINGPORT

void logevent(void *frontend, const char *string);

void vprint(char *fmt, ...)
{	char buf[1024];
	va_list args;
	va_start(args, fmt);
	vsprintf(buf,fmt, args);
	if( buf[strlen(buf)-1]=='\n' ) buf[strlen(buf)-1]='\0';
	logevent(NULL,buf);
	va_end(args);
}

int knock( char *hostname, unsigned short port, unsigned short proto) {
	SOCKET sd;
	struct hostent* host;
	struct sockaddr_in addr;

	host = gethostbyname(hostname);
	if(host == NULL) { fprintf(stderr, "Cannot resolve hostname\n") ; return 1 ; }

	if(proto == PROTO_UDP) { 
		sd = socket(PF_INET, SOCK_DGRAM, 0); 
		if(sd == -1) { fprintf(stderr, "Cannot open socket\n") ; return 2 ; } 
	} else {
		unsigned long arg = !0;
		sd = socket(PF_INET, SOCK_STREAM, 0); 
		if(sd == -1) { fprintf(stderr, "Cannot open socket\n") ; return 3 ; }
		ioctlsocket(sd, FIONBIO, &arg);
	}
	
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = *((long*)host->h_addr_list[0]);
	addr.sin_port = htons(port);
	
	if( proto == PROTO_UDP) {
		vprint("Hitting udp %s:%u\n", inet_ntoa(addr.sin_addr), port);
		connect(sd, (struct sockaddr*)&addr, sizeof(struct sockaddr));
		send(sd, NULL, 0, 0);
	} else {
		vprint("Hitting tcp %s:%u\n", inet_ntoa(addr.sin_addr), port);
		connect(sd, (struct sockaddr*)&addr, sizeof(struct sockaddr));
	}

	shutdown(sd, 2);
	closesocket(sd);
		
	return 0;
	}
	
int ManagePortKnocking( char* host, char *portknockseqorig ) {
	char portstr[256], protostr[256];
	short port, proto ;
	int i,j;
	char * portknockseq = NULL ;
	
	if( (host==NULL) || (portknockseqorig==NULL) ) return 0 ;
	if( (strlen(host)==0) || (strlen(portknockseqorig)==0) ) return 0 ;
	
	portknockseq = (char*)malloc( strlen(portknockseqorig)+1 ) ;
	strcpy(portknockseq,portknockseqorig);
	
	for(i=0;i<strlen(portknockseq);i++) 
		{ if( (portknockseq[i]==' ')||(portknockseq[i]=='	')||(portknockseq[i]==';')||(portknockseq[i]=='-') ) portknockseq[i]=','; }
	while( portknockseq[0]==',' ) del(portknockseq,1,1);
	while( portknockseq[strlen(portknockseq)-1]==',' ) portknockseq[strlen(portknockseq)-1]='\0';
	while( (i=poss(",:",portknockseq)) ) { del(portknockseq,i,1); }
	while( (i=poss(":,",portknockseq)) ) { del(portknockseq,i+1,1); }
	while( (i=poss(",,",portknockseq)) ) { del(portknockseq,i,1); }
	
	while( strlen(portknockseq)>0 ) {
		while( (portknockseq[0]==' ')||(portknockseq[0]=='	')||(portknockseq[0]==',')||(portknockseq[0]==';') ) del(portknockseq,1,1);
		if( strlen(portknockseq)>0 ) {
			i=poss(":",portknockseq) ; if(i==0) i=strlen(portknockseq)+1;
			j=poss(",",portknockseq) ; if(j==0) j=strlen(portknockseq)+1; if(j<i) i=j;
			strcpy(portstr,portknockseq); 
			if( portstr[i-1]!=':' ) { 
				portstr[i-1]='\0'; del(portknockseq,1,i);
				strcpy(protostr,"tcp");
				}
			else {
				portstr[i-1]='\0'; del(portknockseq,1,i);
				i=poss(",",portknockseq) ; if(i==0) i=strlen(portknockseq)+1;
				strcpy(protostr,portknockseq); protostr[i-1]='\0'; del(portknockseq,1,i);
				}
			port=atoi(portstr);
			if( !stricmp(protostr,"udp") ) proto=PROTO_UDP ; else proto=PROTO_TCP ;
			
			if( !stricmp(protostr,"s") ) {
				Sleep( atof(portstr)*1000 ) ; 
			} else {
				if( knock(host,port,proto) ) logevent( NULL, "Unable to knock port" ) ;
				Sleep( 40 ) ;
			}
		}
	}
	
	free(portknockseq);
	return 1;
	}
#endif
