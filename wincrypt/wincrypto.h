#ifdef MOD_WINCRYPT
#ifdef HAS_WINX509
BOOL capi_load_key(const Filename **filename, BinarySink *bs) ;


#define ALG_SID_SHA_256 12
#define ALG_SID_SHA_512 14
#define CALG_SHA_256 (ALG_CLASS_HASH | ALG_TYPE_ANY | ALG_SID_SHA_256)
#define CALG_SHA_512 (ALG_CLASS_HASH | ALG_TYPE_ANY | ALG_SID_SHA_512)

#define CRYPT_ACQUIRE_PREFER_NCRYPT_KEY_FLAG 0x20000
#define HCRYPTPROV_LEGACY void*
typedef unsigned int ALG_ID;
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
