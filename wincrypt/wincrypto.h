#ifdef WINCRYPTPORT
#ifdef USE_CAPI
void* capi_rsa2_sign(void *pRSAKeyStruct, char *data, int datalen);
int capi_is_capikey(void *pRSAKeyStruct);
void *capi_load_key(unsigned char **blob, int *len);
#endif /* USE_CAPI */
#endif
