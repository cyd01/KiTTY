#ifndef KITTY_PROXY
#define KITTY_PROXY
int GetProxySelectionFlag() ;
void SetProxySelectionFlag( const int flag ) ;
int LoadProxyInfo( Conf * conf, const char * name ) ;
void proxy_selection_handler(union control *ctrl, dlgparam *dlg, void *data, int event) ;
void InitProxyList(void) ;
int LoadProxyInfo( Conf * conf, const char * name ) ;
#endif
