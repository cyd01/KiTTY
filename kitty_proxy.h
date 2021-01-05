#ifndef KITTY_PROXY
#define KITTY_PROXY

#define MAX_PROXY 100
struct Proxies {
	char *name;
	int val;
};
extern struct Proxies proxies[MAX_PROXY] ;
    
int GetProxySelectionFlag() ;
void SetProxySelectionFlag( const int flag ) ;
int LoadProxyInfo( Conf * conf, const char * name ) ;
void InitProxyList(void) ;
int LoadProxyInfo( Conf * conf, const char * name ) ;
#endif
