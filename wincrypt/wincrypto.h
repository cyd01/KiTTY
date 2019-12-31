#ifdef MOD_WINCRYPT
#ifdef HAS_WINX509
BOOL capi_load_key(const Filename **filename, BinarySink *bs) ;
#define HCRYPTPROV_LEGACY void*
BOOL CryptHashCertificate(
  HCRYPTPROV_LEGACY hCryptProv,
  ALG_ID            Algid,
  DWORD             dwFlags,
  const BYTE        *pbEncoded,
  DWORD             cbEncoded,
  BYTE              *pbComputedHash,
  DWORD             *pcbComputedHash
);
#endif /* HAS_WINX509 */
#endif
