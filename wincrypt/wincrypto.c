#ifdef MOD_WINCRYPT
/*
 * PuTTY wincrypt patch main file.
 * Author: Ulf Frisk, puttywincrypt@ulffrisk.com
 */
#include "putty.h"
#include "ssh.h"
#include "mpint.h"

#include <windows.h>
#include <wincrypt.h>
//#include <bcrypt.h>
//#include <ncrypt.h>
#include "wincrypt/wincrypto.h"

 /*
  * Defines and declarations due to missing declarations in mingw32 and BCC55 headers.
  */
#ifndef CERT_SYSTEM_STORE_CURRENT_USER
#define CERT_SYSTEM_STORE_CURRENT_USER (1 << 16)
#endif /* CERT_SYSTEM_STORE_CURRENT_USER */

#ifndef CERT_STORE_PROV_MEMORY
#define CERT_STORE_PROV_MEMORY ((LPCSTR) 2)
#endif /* CERT_STORE_PROV_MEMORY */

#ifndef CRYPT_FIND_USER_KEYSET_FLAG
#define CRYPT_FIND_USER_KEYSET_FLAG 0x00000001
#endif /* CRYPT_FIND_USER_KEYSET_FLAG */

#ifndef CRYPT_FIND_SILENT_KEYSET_FLAG
#define CRYPT_FIND_SILENT_KEYSET_FLAG 0x00000040
#endif /* CRYPT_FIND_SILENT_KEYSET_FLAG */

#ifndef CERT_CLOSE_STORE_FORCE_FLAG
#define CERT_CLOSE_STORE_FORCE_FLAG 0x00000001
#endif /* CERT_CLOSE_STORE_FORCE_FLAG */

#ifndef CRYPT_ACQUIRE_NO_HEALING
#define CRYPT_ACQUIRE_NO_HEALING 0x00000008
#endif /* CRYPT_ACQUIRE_NO_HEALING */

#ifndef CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG
#define CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG 0x00010000
#endif /* CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG */

#ifndef CERT_NCRYPT_KEY_SPEC
#define CERT_NCRYPT_KEY_SPEC 0xFFFFFFFF
#endif /* CERT_NCRYPT_KEY_SPEC */

typedef ULONG_PTR HCRYPTPROV_OR_NCRYPT_KEY_HANDLE;

#ifndef __BCRYPT_H__
#define BCRYPT_PAD_PKCS1 0x00000002
#define BCRYPT_SHA1_ALGORITHM L"SHA1"
typedef struct _BCRYPT_PKCS1_PADDING_INFO
{
	LPCWSTR pszAlgId;
} BCRYPT_PKCS1_PADDING_INFO;
#endif /* __BCRYPT_H__ */

#ifndef CRYPT_ACQUIRE_SILENT_FLAG
#define CRYPT_ACQUIRE_SILENT_FLAG 0x00000040
#endif /* CRYPT_ACQUIRE_SILENT_FLAG */

typedef PCCERT_CONTEXT(WINAPI *DFNCryptUIDlgSelectCertificateFromStore)(HCERTSTORE, HWND, LPCWSTR, LPCWSTR, DWORD, DWORD, PVOID);

/*
 * Dynamically lookup NCryptSignHash to avoid link dependency to ncrypt.dll (not supported by Windows XP).
 */
typedef LONG(WINAPI *DFNNCryptSignHash)(ULONG_PTR, PVOID, PBYTE, DWORD, PBYTE, DWORD, PDWORD, DWORD);

/*
 * convert sha1 string to binary data
 */
void capi_sha1_to_binary(PSTR szHex, PBYTE pbBin)
{
	unsigned char i, h, l;
	for (i = 0; i < 20; i++) {
		h = szHex[i << 1];
		l = szHex[(i << 1) + 1];
		pbBin[i] =
			(((h >= '0' && h <= '9') ? h - '0' : ((h >= 'a' && h <= 'f') ? h - 'a' + 10 : ((h >= 'A' && h <= 'F') ? h - 'A' + 10 : 0))) << 4) +
			(((l >= '0' && l <= '9') ? l - '0' : ((l >= 'a' && l <= 'f') ? l - 'a' + 10 : ((l >= 'A' && l <= 'F') ? l - 'A' + 10 : 0))));
	}
}

/*
 * Windows XP do not support CryptBinaryToString with raw hex.
 */
PSTR capi_binary_to_hex(PBYTE pbBinary, DWORD cbBinary)
{
	PSTR szHex;
	DWORD idx;
	BYTE b;
	szHex = snewn((cbBinary << 1) + 1, char);
	szHex[cbBinary << 1] = 0;
	for (idx = 0; idx < cbBinary; idx++) {
		b = (pbBinary[idx] >> 4) & 0x0F;
		szHex[idx << 1] = (b < 10) ? b + '0' : b - 10 + 'a';
		b = pbBinary[idx] & 0x0F;
		szHex[(idx << 1) + 1] = (b < 10) ? b + '0' : b - 10 + 'a';
	}
	return szHex;
}

/*
 * Reverse a byte array.
 */
void capi_reverse_array(PBYTE pb, DWORD cb)
{
	DWORD i;
	BYTE t;
	for (i = 0; i < cb >> 1; i++) {
		t = pb[i];
		pb[i] = pb[cb - i - 1];
		pb[cb - i - 1] = t;
	}
}

/*
 * Select a certificate given the criteria provided.
 * If a criterion is absent it will be disregarded.
 */
void capi_select_cert_2(PBYTE pbSHA1, LPWSTR wszCN, PCCERT_CONTEXT *ppCertCtx, HCERTSTORE *phStore)
{
	HCERTSTORE hStoreMY = NULL, hStoreTMP = NULL;
	PCCERT_CONTEXT pCertCtx = NULL;
	HMODULE hCryptUIDLL = NULL;
	DFNCryptUIDlgSelectCertificateFromStore dfnCryptUIDlgSelectCertificateFromStore;
	CRYPT_HASH_BLOB cryptHashBlob;
	DWORD dwCertCount = 0;
	if (!(hStoreMY = CertOpenStore((LPCSTR)CERT_STORE_PROV_SYSTEM, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"MY"))) {
		goto error;
	}
	if (pbSHA1) {
		cryptHashBlob.cbData = 20;
		cryptHashBlob.pbData = pbSHA1;
		if ((*ppCertCtx = CertFindCertificateInStore(hStoreMY, X509_ASN_ENCODING, 0, CERT_FIND_SHA1_HASH, &cryptHashBlob, pCertCtx))) {
			*phStore = hStoreMY;
			return;
		} else {
			goto error;
		}
	}
	if (!(hStoreTMP = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL))) {
		goto error;
	}
	while (TRUE) {
		if (wszCN) {
			pCertCtx = CertFindCertificateInStore(hStoreMY, X509_ASN_ENCODING, 0, CERT_FIND_SUBJECT_STR, wszCN, pCertCtx);
		} else {
			pCertCtx = CertEnumCertificatesInStore(hStoreMY, pCertCtx);
		}
		if (!pCertCtx) {
			break;
		}
		/*
		if (!CryptAcquireCertificatePrivateKey(pCertCtx, CRYPT_ACQUIRE_CACHE_FLAG | CRYPT_ACQUIRE_NO_HEALING | CRYPT_ACQUIRE_SILENT_FLAG | CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG, NULL, &hCryptProvOrNCryptKey, &dwKeySpec, &fCallerFreeProvAlwaysFalse)) {
			continue;
		}
		*/
		dwCertCount++;
		CertAddCertificateContextToStore(hStoreTMP, pCertCtx, CERT_STORE_ADD_ALWAYS, NULL);
	}
	CertCloseStore(hStoreMY, CERT_CLOSE_STORE_FORCE_FLAG);
	hStoreMY = NULL;
	if (dwCertCount == 1) {
		*ppCertCtx = CertEnumCertificatesInStore(hStoreTMP, NULL);
		*phStore = hStoreTMP;
		return;
	} else if ((dwCertCount > 1) &&
		(hCryptUIDLL = LoadLibrary("cryptui.dll")) &&
		(dfnCryptUIDlgSelectCertificateFromStore = (DFNCryptUIDlgSelectCertificateFromStore)GetProcAddress(hCryptUIDLL, "CryptUIDlgSelectCertificateFromStore")) &&
		(*ppCertCtx = dfnCryptUIDlgSelectCertificateFromStore(hStoreTMP, NULL, NULL, NULL, 0, 0, NULL))) {
		*phStore = hStoreTMP;
		FreeLibrary(hCryptUIDLL);
		return;
	}
error:
	if (hCryptUIDLL) { FreeLibrary(hCryptUIDLL); }
	if (hStoreTMP) { CertCloseStore(hStoreTMP, CERT_CLOSE_STORE_FORCE_FLAG); }
	if (hStoreMY) { CertCloseStore(hStoreMY, CERT_CLOSE_STORE_FORCE_FLAG); }
	*ppCertCtx = NULL;
	*phStore = NULL;
}

/*
 * Return a malloc'ed string containing the requested subitem.
 */
PSTR capi_select_cert_finditem(PSTR szCert, PCSTR szStart)
{
	PSTR ptrStart, ptrEnd, szResult;
	ptrStart = strstr(szCert, szStart);
	ptrEnd = strstr(szCert, ",");
	if (!ptrEnd || ptrEnd < ptrStart) {
		ptrEnd = szCert + strlen(szCert);
	}
	if (!ptrStart || ptrStart > ptrEnd) {
		return NULL;
	}
	ptrStart += strlen(szStart);
	szResult = (PSTR)calloc(ptrEnd - ptrStart + 1, sizeof(char));
	memcpy(szResult, ptrStart, ptrEnd - ptrStart);
	return szResult;
}

/*
 * Select a certificate given the definition string.
 */
void capi_select_cert(PSTR szCert, PCCERT_CONTEXT *ppCertCtx, HCERTSTORE *phStore)
{
	PSTR szCN = NULL, szThumb, ptrStart, ptrStartAll;
	LPWSTR wszCN = NULL;
	DWORD i, len;
	PBYTE pbThumb = snewn(20, BYTE);
	ptrStart = strstr(szCert, "cert://");
	ptrStartAll = strstr(szCert, "cert://*");
	if (ptrStart != szCert) {
		ptrStart = strstr(szCert, "x509://");
		ptrStartAll = strstr(szCert, "x509://*");
		if (ptrStart != szCert) {
			*ppCertCtx = NULL;
			*phStore = NULL;
			return;
		}
	}
	if (ptrStartAll) {
		capi_select_cert_2(NULL, NULL, ppCertCtx, phStore);
		return;
	}
	szThumb = capi_select_cert_finditem(szCert, "thumbprint=");
	if (szThumb && 40 == strlen(szThumb)) {
		capi_sha1_to_binary(szThumb, pbThumb);
		capi_select_cert_2(pbThumb, NULL, ppCertCtx, phStore);
	} else {
		szCN = capi_select_cert_finditem(szCert, "cn=");
		if (szCN) {
			len = strlen(szCN);
			wszCN = (LPWSTR)calloc(len + 1, sizeof(wchar_t));
			for (i = 0; i < len; i++) {
				wszCN[i] = szCN[i];
			}
		}
		capi_select_cert_2(NULL, wszCN, ppCertCtx, phStore);
	}
	if (szCN) { free(szCN); }
	if (wszCN) { free(wszCN); }
	sfree(pbThumb);
}

/*
 * Get rsa key comment on the form "cert://cn=<cn>,thumbprint=<sha1>".
 */
static PSTR capi_get_description(PSTR file, PCCERT_CONTEXT pCertContext)
{
	DWORD thumbPrintSize = 20;
	BYTE thumbPrint[20];
	if (!CryptHashCertificate(0, CALG_SHA1, 0, pCertContext->pbCertEncoded,
		pCertContext->cbCertEncoded, (PBYTE)&thumbPrint, &thumbPrintSize)) {
	}
	PSTR tp = capi_binary_to_hex((PBYTE)&thumbPrint, thumbPrintSize);
	return dupcat(file, "thumbprint=", tp, NULL);
}

char *wincrypto_invalid(ssh_key *key, unsigned flags)
{
	RSAKey *rsa = container_of(key, RSAKey, sshk);
	HCERTSTORE hCertStore;
	PCCERT_CONTEXT pCertCtx;
	HCRYPTPROV_OR_NCRYPT_KEY_HANDLE hCryptProvOrNCryptKey = 0;
	DWORD dwSpec, cbSig = 0;
	BOOL fCallerFreeProvAlwaysFalse = TRUE;

	capi_select_cert(rsa->comment, &pCertCtx, &hCertStore);
	if (pCertCtx)
	{
		if (CryptAcquireCertificatePrivateKey(pCertCtx, CRYPT_ACQUIRE_CACHE_FLAG | CRYPT_ACQUIRE_PREFER_NCRYPT_KEY_FLAG, 0, &hCryptProvOrNCryptKey, &dwSpec, &fCallerFreeProvAlwaysFalse)) {
			return NULL;
		}
		CertFreeCertificateContext(pCertCtx);
		CertCloseStore(hCertStore, CERT_CLOSE_STORE_FORCE_FLAG);
	}
	return dupstr("Could not acquire private key..");
}

static void wincrypto_public_blob(ssh_key *key, BinarySink *bs)
{
	DWORD cbPublicKeyBlob = 8192;
	PBYTE pbPublicKeyBlob = NULL;
	bool isX509 = false;
	uintmax_t size = 0, i = 0;

	RSAKey *rsa = container_of(key, RSAKey, sshk);

	if (0 == strncmp("x509://", rsa->comment, 7)) {
		isX509 = true;
	}

	if (isX509) {
		size = mp_get_integer(rsa->private_exponent);
		for (i = 0; i < size; i++) {
			BYTE f = mp_get_byte(rsa->iqmp, (size_t)(size - i - 1));
			put_byte(bs, f);
		}
	} else {
		put_stringz(bs, "ssh-rsa");
		put_mp_ssh2(bs, rsa->exponent);
		put_mp_ssh2(bs, rsa->modulus);
	}

}

static ssh_key *wincrypto_new_priv(const ssh_keyalg *self,
	ptrlen pub, ptrlen priv)
{
	RSAKey *rsa;
	BOOL result;
	PCCERT_CONTEXT pCertContext;
	HCERTSTORE hCertStore;
	DWORD cbPublicKeyBlob = 8192;
	PBYTE pbPublicKeyBlob = NULL;
	RSAPUBKEY *pRSAPubKey;
	bool isX509 = false;

	int len = strlen(pub.ptr);
	if ((len < 7)
		|| !(0 == strncmp("cert://", pub.ptr, 7)
			|| (isX509 = (0 == strncmp("x509://", pub.ptr, 7))))) {
		return NULL;
	}

	capi_select_cert((PSTR)pub.ptr, &pCertContext, &hCertStore);
	if (!pCertContext) {
		return NULL;
	}

	rsa = snew(RSAKey);
	rsa->p = mp_from_integer(0);
	rsa->q = mp_from_integer(0);
	if (isX509)
		rsa->sshk.vt = &ssh_x509_wincrypt;
	else
		rsa->sshk.vt = &ssh_rsa_wincrypt;
	rsa->comment = dupstr(pub.ptr);

	result = CryptDecodeObject(
		X509_ASN_ENCODING,
		RSA_CSP_PUBLICKEYBLOB,
		pCertContext->pCertInfo->SubjectPublicKeyInfo.PublicKey.pbData,
		pCertContext->pCertInfo->SubjectPublicKeyInfo.PublicKey.cbData,
		0,
		(void*)(pbPublicKeyBlob = snewn(cbPublicKeyBlob, BYTE)),
		&cbPublicKeyBlob);
	if (!result) {
		CertFreeCertificateContext(pCertContext);
		sfree(pbPublicKeyBlob);
		return NULL;
	}

	pRSAPubKey = (RSAPUBKEY*)(pbPublicKeyBlob + sizeof(BLOBHEADER));
	capi_reverse_array(pbPublicKeyBlob + sizeof(BLOBHEADER) + sizeof(RSAPUBKEY), pRSAPubKey->bitlen / 8);
	rsa->exponent = mp_from_integer(pRSAPubKey->pubexp);
	rsa->modulus = mp_from_bytes_be(make_ptrlen(pbPublicKeyBlob + sizeof(BLOBHEADER) + sizeof(RSAPUBKEY), pRSAPubKey->bitlen / 8));
	rsa->iqmp = mp_from_bytes_be(make_ptrlen(pCertContext->pbCertEncoded, pCertContext->cbCertEncoded));
	rsa->private_exponent = mp_from_integer(pCertContext->cbCertEncoded);

	// cleanup
	sfree(pbPublicKeyBlob);
	CertFreeCertificateContext(pCertContext);
	CertCloseStore(hCertStore, CERT_CLOSE_STORE_FORCE_FLAG);

	return &rsa->sshk;
}

static void wincrypto_sign(ssh_key *key, ptrlen data,
	unsigned flags, BinarySink *bs)
{
	HCERTSTORE hCertStore;
	PCCERT_CONTEXT pCertCtx;
	HCRYPTPROV_OR_NCRYPT_KEY_HANDLE hCryptProvOrNCryptKey = 0;
	HCRYPTHASH hHash = 0;
	PBYTE pbSig = NULL;
	DWORD dwSpec, cbSig = 0;
	BOOL fCallerFreeProvAlwaysFalse = TRUE;
	bool isX509 = false;
	BCRYPT_PKCS1_PADDING_INFO padInfo;
	BCRYPT_ALG_HANDLE       hHashAlg = NULL;
	BCRYPT_HASH_HANDLE      hHashBcrypt = NULL;
	NTSTATUS                status = 0;
	DWORD                   cbData = 0, cbHash = 0, cbHashObject = 0;
	PBYTE                   pbHashObject = NULL;
	PBYTE                   pbHash = NULL;

	unsigned short* bcrypt_alg = NULL;
	ALG_ID alg = CALG_SHA1;
	const char *sign_alg_name;
	RSAKey *rsa = container_of(key, RSAKey, sshk);

	int len = strlen(rsa->comment);
	if ((len < 7)
		|| !(0 == strncmp("cert://", rsa->comment, 7)
			|| (isX509 = (0 == strncmp("x509://", rsa->comment, 7))))) {
		return;
	}

	if (isX509) {
		alg = CALG_SHA1;
		bcrypt_alg = BCRYPT_SHA1_ALGORITHM;
		padInfo.pszAlgId = BCRYPT_SHA1_ALGORITHM;
		sign_alg_name = "x509v3-sign-rsa";
	} else {
		if (flags & SSH_AGENT_RSA_SHA2_256) {
			alg = CALG_SHA_256;
			bcrypt_alg = BCRYPT_SHA256_ALGORITHM;
			padInfo.pszAlgId = BCRYPT_SHA256_ALGORITHM;
			sign_alg_name = "rsa-sha2-256";
		} else if (flags & SSH_AGENT_RSA_SHA2_512) {
			alg = CALG_SHA_512;
			bcrypt_alg = BCRYPT_SHA512_ALGORITHM;
			padInfo.pszAlgId = BCRYPT_SHA512_ALGORITHM;
			sign_alg_name = "rsa-sha2-512";
		} else {
			alg = CALG_SHA1;
			bcrypt_alg = BCRYPT_SHA1_ALGORITHM;
			padInfo.pszAlgId = BCRYPT_SHA1_ALGORITHM;
			sign_alg_name = "ssh-rsa";
		}
	}

	capi_select_cert(rsa->comment, &pCertCtx, &hCertStore);
	if (pCertCtx)
	{
		if (CryptAcquireCertificatePrivateKey(pCertCtx, CRYPT_ACQUIRE_CACHE_FLAG | CRYPT_ACQUIRE_PREFER_NCRYPT_KEY_FLAG, 0, &hCryptProvOrNCryptKey, &dwSpec, &fCallerFreeProvAlwaysFalse)) {
			if (dwSpec == AT_KEYEXCHANGE || dwSpec == AT_SIGNATURE) {
				/* A lot faster for smartcards because CryptSignHash for asking buffersize is querying sc already */
				cbSig = 2048;
				pbSig = snewn(cbSig, BYTE);

				/* CSP implementation */
				if (!CryptCreateHash((HCRYPTPROV)hCryptProvOrNCryptKey, alg, 0, 0, &hHash)) {
					goto Cleanup;
				}

				if (!CryptHashData(hHash, data.ptr, data.len, 0)) {
					goto Cleanup;
				}

				if (!CryptSignHash(hHash, dwSpec, NULL, 0, pbSig, &cbSig)) {
					goto Cleanup;
				}

				capi_reverse_array(pbSig, cbSig);
				put_stringz(bs, sign_alg_name);
				put_uint32(bs, cbSig);
				put_data(bs, pbSig, cbSig);
			} else if (dwSpec == CERT_NCRYPT_KEY_SPEC) {
				/* KSP/CNG implementation */

				if ((status = BCryptOpenAlgorithmProvider(
					&hHashAlg, bcrypt_alg, NULL, 0)) != 0) {
					goto Cleanup;
				}

				if ((status = BCryptGetProperty(
					hHashAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&cbHashObject, sizeof(DWORD), &cbData, 0)) != 0) {
					goto Cleanup;
				}

				pbHashObject = snewn(cbHashObject, BYTE);
				if (NULL == pbHashObject) {
					goto Cleanup;
				}

				if ((status = BCryptGetProperty(
					hHashAlg, BCRYPT_HASH_LENGTH, (PBYTE)&cbHash, sizeof(DWORD), &cbData, 0)) != 0) {
					goto Cleanup;
				}

				pbHash = snewn(cbHash, BYTE);
				if (NULL == pbHash) {
					goto Cleanup;
				}

				if ((status = BCryptCreateHash(
					hHashAlg, &hHashBcrypt, pbHashObject, cbHashObject, NULL, 0, 0)) != 0) {
					goto Cleanup;
				}

				if ((status = BCryptHashData(
					hHashBcrypt, (PUCHAR)data.ptr, data.len, 0)) != 0) {
					goto Cleanup;
				}

				if ((status = BCryptFinishHash(
					hHashBcrypt, pbHash, cbHash, 0)) != 0) {
					goto Cleanup;
				}

				if ((status = NCryptSignHash(
					hCryptProvOrNCryptKey, &padInfo, pbHash, cbHash, NULL, 0, &cbSig, BCRYPT_PAD_PKCS1)) != 0) {
					goto Cleanup;
				}

				pbSig = snewn(cbSig, BYTE);
				if (NULL == pbSig) {
					goto Cleanup;
				}

				if ((status = NCryptSignHash(
					hCryptProvOrNCryptKey, &padInfo, pbHash, cbHash, pbSig, cbSig, &cbSig, BCRYPT_PAD_PKCS1)) != 0) {
					goto Cleanup;
				}

				put_stringz(bs, sign_alg_name);
				put_uint32(bs, cbSig);
				put_data(bs, pbSig, cbSig);
			}
		}
	}
Cleanup:
	if (hHashAlg)
		BCryptCloseAlgorithmProvider(hHashAlg, 0);
	if (hHash)
		BCryptDestroyHash(hHashBcrypt);
	if (pbHashObject)
		sfree(pbHashObject);
	if (pbHash)
		sfree(pbHash);
	if (pbSig)
		sfree(pbSig);
	if (hHash)
		CryptDestroyHash(hHash);
	if (pCertCtx)
		CertFreeCertificateContext(pCertCtx);
	if (hCertStore)
		CertCloseStore(hCertStore, CERT_CLOSE_STORE_FORCE_FLAG);
}

static void wincrypto_freekey(ssh_key *key)
{
	RSAKey *rsa = container_of(key, RSAKey, sshk);
	freersakey(rsa);
	sfree(rsa);
}

/*
 * Load a rsa key from a certificate in windows certificate personal store.
 */
BOOL capi_load_key(const Filename **filename, BinarySink *bs)
{
	BOOL result;
	PCCERT_CONTEXT pCertContext;
	HCERTSTORE hCertStore;
	DWORD cbPublicKeyBlob = 8192;
	PBYTE pbPublicKeyBlob = NULL;
	RSAPUBKEY *pRSAPubKey;
	bool isX509 = false;

	int len = strlen((*filename)->path);
	if ((len < 7)
		|| !(0 == strncmp("cert://", (*filename)->path, 7)
			|| (isX509 = (0 == strncmp("x509://", (*filename)->path, 7))))) {
		return false;
	}
	capi_select_cert((*filename)->path, &pCertContext, &hCertStore);
	if (!pCertContext) {
		return false;
	}

	if ((*filename)->path[7] == '*') {
		(*filename)->path[7] = '\0';
		(*filename) = filename_from_str(capi_get_description((*filename)->path, pCertContext));
	}

	result = CryptDecodeObject(
		X509_ASN_ENCODING,
		RSA_CSP_PUBLICKEYBLOB,
		pCertContext->pCertInfo->SubjectPublicKeyInfo.PublicKey.pbData,
		pCertContext->pCertInfo->SubjectPublicKeyInfo.PublicKey.cbData,
		0,
		(void*)(pbPublicKeyBlob = snewn(cbPublicKeyBlob, BYTE)),
		&cbPublicKeyBlob);
	if (!result) {
		CertFreeCertificateContext(pCertContext);
		sfree(pbPublicKeyBlob);
		return false;
	}

	pRSAPubKey = (RSAPUBKEY*)(pbPublicKeyBlob + sizeof(BLOBHEADER));
	if (isX509) {
		put_data(bs, pCertContext->pbCertEncoded, pCertContext->cbCertEncoded);
	} else {
		put_stringz(bs, "ssh-rsa");
		put_uint32(bs, 3);
		capi_reverse_array((PBYTE)&(pRSAPubKey->pubexp), 4);
		put_uint32(bs, pRSAPubKey->pubexp);
		capi_reverse_array(pbPublicKeyBlob + sizeof(BLOBHEADER) + sizeof(RSAPUBKEY), pRSAPubKey->bitlen / 8);
		put_uint16(bs, 1);
		put_uint16(bs, pRSAPubKey->bitlen / 8);
		put_data(bs, pbPublicKeyBlob + sizeof(BLOBHEADER) + sizeof(RSAPUBKEY), pRSAPubKey->bitlen / 8);
	}

	/* cleanup */
	sfree(pbPublicKeyBlob);
	CertFreeCertificateContext(pCertContext);
	CertCloseStore(hCertStore, CERT_CLOSE_STORE_FORCE_FLAG);
	return true;
}

const ssh_keyalg ssh_rsa_wincrypt = {
	NULL /*rsa2_new_pub*/,
	wincrypto_new_priv /*rsa2_new_priv*/,
	NULL /*rsa2_new_priv_openssh*/,

	wincrypto_freekey /*rsa2_freekey*/,
	wincrypto_invalid,
	wincrypto_sign /*rsa2_sign*/,
	NULL /*rsa2_verify*/,
	wincrypto_public_blob /*rsa2_public_blob*/,
	NULL /*rsa2_private_blob*/,
	NULL /*rsa2_openssh_blob*/,
	NULL /*rsa2_cache_str*/,

	NULL /*rsa2_pubkey_bits*/,

	"ssh-rsa",
	"rsa2",
	NULL,
	SSH_AGENT_RSA_SHA2_256 | SSH_AGENT_RSA_SHA2_512,
};


const ssh_keyalg ssh_x509_wincrypt = {
	NULL /*rsa2_new_pub*/,
	wincrypto_new_priv /*rsa2_new_priv*/,
	NULL /*rsa2_new_priv_openssh*/,

	wincrypto_freekey /*rsa2_freekey*/,
	wincrypto_invalid,
	wincrypto_sign /*rsa2_sign*/,
	NULL /*rsa2_verify*/,
	wincrypto_public_blob /*rsa2_public_blob*/,
	NULL /*rsa2_private_blob*/,
	NULL /*rsa2_openssh_blob*/,
	NULL /*rsa2_cache_str*/,

	NULL /*rsa2_pubkey_bits*/,

	"x509v3-sign-rsa",
	"rsa2",
	NULL,
	SSH_AGENT_RSA_SHA2_256 | SSH_AGENT_RSA_SHA2_512,
};

#endif
