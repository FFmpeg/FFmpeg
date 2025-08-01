/*
 * Copyright (c) 2015 Hendrik Leppkes
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/** Based on the CURL SChannel module */

#include "config.h"
#include "config_components.h"

#include "libavutil/mem.h"
#include "avformat.h"
#include "internal.h"
#include "network.h"
#include "os_support.h"
#include "url.h"
#include "tls.h"

#define SECURITY_WIN32
#include <windows.h>
#include <security.h>
#include <schnlsp.h>
#include <sddl.h>

#define SCHANNEL_INITIAL_BUFFER_SIZE   4096
#define SCHANNEL_FREE_BUFFER_SIZE      1024

/* mingw does not define this symbol */
#ifndef SECBUFFER_ALERT
#define SECBUFFER_ALERT                17
#endif

/* This is the name used for the private key in the MS Keystore.
 * There is as of time of writing no way to use schannel without
 * persisting the private key. Which usually means the default MS
 * keystore will write it to disk unencrypted, user-read/writable.
 * To combat this as much as possible, the code makes sure to
 * delete the private key ASAP once SChannel has gotten ahold of
 * it.
 * Apparently this is because SChannel neglects marshaling the
 * private key alongside the certificate for the out-of-process
 * tls handler.
 * See this GitHub issue for the most detailed explanation out there:
 * https://github.com/dotnet/runtime/issues/23749#issuecomment-485947319
 */
#define FF_NCRYPT_TEMP_KEY_NAME L"FFMPEG_TEMP_TLS_KEY"

static int der_to_pem(const char *data, size_t len, const char *header, char *buf, size_t bufsize)
{
    const int line_length = 64;
    AVBPrint pem;
    DWORD base64len = 0;
    char *base64 = NULL;
    int ret = 0;

    if (!CryptBinaryToStringA(data, len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &base64len)) {
        av_log(NULL, AV_LOG_ERROR, "CryptBinaryToString failed\n");
        ret = AVERROR_EXTERNAL;
        goto end;
    }

    base64 = av_malloc(base64len);

    if (!CryptBinaryToStringA(data, len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, base64, &base64len)) {
        av_log(NULL, AV_LOG_ERROR, "CryptBinaryToString failed\n");
        ret = AVERROR_EXTERNAL;
        goto end;
    }

    av_bprint_init_for_buffer(&pem, buf, bufsize);
    av_bprintf(&pem, "-----BEGIN %s-----\n", header);

    for (DWORD i = 0; i < base64len; i += line_length) {
        av_bprintf(&pem, "%.*s\n", line_length, base64 + i);
    }

    av_bprintf(&pem, "-----END %s-----\n", header);

    if (!av_bprint_is_complete(&pem)) {
        ret = AVERROR(ENOSPC);
        goto end;
    }

end:
    av_free(base64);
    return ret;
}

static int pem_to_der(const char *pem, char **buf, int *out_len)
{
    DWORD derlen = 0;

    if (!CryptStringToBinaryA(pem, 0, CRYPT_STRING_BASE64HEADER, NULL, &derlen, NULL, NULL)) {
        av_log(NULL, AV_LOG_ERROR, "CryptStringToBinaryA failed\n");
        return AVERROR(EINVAL);
    }

    *buf = av_malloc(derlen);
    if (!*buf)
        return AVERROR(ENOMEM);

    if (!CryptStringToBinaryA(pem, 0, CRYPT_STRING_BASE64HEADER, *buf, &derlen, NULL, NULL)) {
        av_log(NULL, AV_LOG_ERROR, "CryptStringToBinaryA failed\n");
        return AVERROR(EINVAL);
    }

    *out_len = derlen;

    return 0;
}

static int der_to_fingerprint(const char *data, size_t len, char **fingerprint)
{
    AVBPrint buf;
    unsigned char hash[32];
    DWORD hashsize = sizeof(hash);

    if (!CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM, 0, NULL, data, len, hash, &hashsize))
    {
        av_log(NULL, AV_LOG_ERROR, "CryptHashCertificate2 failed\n");
        return AVERROR_EXTERNAL;
    }

    av_bprint_init(&buf, hashsize*3, hashsize*3);

    for (int i = 0; i < hashsize - 1; i++)
        av_bprintf(&buf, "%02X:", hash[i]);
    av_bprintf(&buf, "%02X", hash[hashsize - 1]);

    return av_bprint_finalize(&buf, fingerprint);
}

static int tls_gen_self_signed(NCRYPT_KEY_HANDLE *key, PCCERT_CONTEXT *crtctx)
{
    NCRYPT_PROV_HANDLE provider = 0;
    CERT_NAME_BLOB subject = { 0 };

    DWORD export_props = NCRYPT_ALLOW_EXPORT_FLAG | NCRYPT_ALLOW_PLAINTEXT_EXPORT_FLAG;
    DWORD usage_props = NCRYPT_ALLOW_ALL_USAGES;
    LPCSTR ext_usages[] = { szOID_PKIX_KP_SERVER_AUTH };
    BYTE key_usage = CERT_KEY_ENCIPHERMENT_KEY_USAGE | CERT_DIGITAL_SIGNATURE_KEY_USAGE;
    CRYPT_BIT_BLOB key_usage_blob = { 0 };
    CERT_ENHKEY_USAGE eku = { 0 };
    CERT_BASIC_CONSTRAINTS2_INFO basic_constraints = { 0 };
    CERT_ALT_NAME_ENTRY san_entry = { 0 };
    CERT_ALT_NAME_INFO san_info = { 0 };
    CERT_EXTENSION ext[4] = { 0 };
    CERT_EXTENSIONS exts = { 0 };
    CRYPT_ALGORITHM_IDENTIFIER sig_alg = { (LPSTR)szOID_ECDSA_SHA256 };
    CRYPT_KEY_PROV_INFO prov_info = { 0 };
    const char *subj_str = "CN=lavf";

    SECURITY_STATUS sspi_ret;
    int ret = 0;

    *crtctx = NULL;

    sspi_ret = NCryptOpenStorageProvider(&provider, MS_KEY_STORAGE_PROVIDER, 0);
    if (sspi_ret != ERROR_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "NCryptOpenStorageProvider failed(0x%lx)\n", sspi_ret);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    sspi_ret = NCryptCreatePersistedKey(provider, key, BCRYPT_ECDSA_P256_ALGORITHM, FF_NCRYPT_TEMP_KEY_NAME, 0, NCRYPT_OVERWRITE_KEY_FLAG);
    if (sspi_ret != ERROR_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "NCryptCreatePersistedKey failed(0x%lx)\n", sspi_ret);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    sspi_ret = NCryptSetProperty(*key, NCRYPT_EXPORT_POLICY_PROPERTY, (PBYTE)&export_props, sizeof(export_props), 0);
    if (sspi_ret != ERROR_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "NCryptSetProperty(NCRYPT_EXPORT_POLICY_PROPERTY) failed(0x%lx)\n", sspi_ret);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    sspi_ret = NCryptSetProperty(*key, NCRYPT_KEY_USAGE_PROPERTY, (PBYTE)&usage_props, sizeof(usage_props), 0);
    if (sspi_ret != ERROR_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "NCryptSetProperty(NCRYPT_KEY_USAGE_PROPERTY) failed(0x%lx)\n", sspi_ret);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    sspi_ret = NCryptFinalizeKey(*key, 0);
    if (sspi_ret != ERROR_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "NCryptFinalizeKey failed(0x%lx)\n", sspi_ret);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    if (!CertStrToNameA(X509_ASN_ENCODING, subj_str, 0, NULL, NULL, &subject.cbData, NULL))
    {
        av_log(NULL, AV_LOG_ERROR, "Initial subj init failed\n");
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    subject.pbData = av_malloc(subject.cbData);
    if (!subject.pbData) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (!CertStrToNameA(X509_ASN_ENCODING, subj_str, 0, NULL, subject.pbData, &subject.cbData, NULL))
    {
        av_log(NULL, AV_LOG_ERROR, "Subj init failed\n");
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    // Extended Key Usage extension
    eku.cUsageIdentifier = 1;
    eku.rgpszUsageIdentifier = (LPSTR*)ext_usages;

    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_ENHANCED_KEY_USAGE, &eku,
                             CRYPT_ENCODE_ALLOC_FLAG, NULL, &ext[0].Value.pbData, &ext[0].Value.cbData)) {
        av_log(NULL, AV_LOG_ERROR, "CryptEncodeObjectEx for EKU failed\n");
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    ext[0].pszObjId = (LPSTR)szOID_ENHANCED_KEY_USAGE;
    ext[0].fCritical = TRUE;

    // Key usage extension
    key_usage_blob.cbData = sizeof(key_usage);
    key_usage_blob.pbData = &key_usage;

    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_BITS, &key_usage_blob,
                             CRYPT_ENCODE_ALLOC_FLAG, NULL, &ext[1].Value.pbData, &ext[1].Value.cbData)) {
        av_log(NULL, AV_LOG_ERROR, "CryptEncodeObjectEx for KU failed\n");
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    ext[1].pszObjId = (LPSTR)szOID_KEY_USAGE;
    ext[1].fCritical = TRUE;

    // Cert Basic Constraints
    basic_constraints.fCA = FALSE;

    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_BASIC_CONSTRAINTS2, &basic_constraints,
                             CRYPT_ENCODE_ALLOC_FLAG, NULL, &ext[2].Value.pbData, &ext[2].Value.cbData)) {
        av_log(NULL, AV_LOG_ERROR, "CryptEncodeObjectEx for KU failed\n");
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    ext[2].pszObjId = (LPSTR)szOID_BASIC_CONSTRAINTS2;
    ext[2].fCritical = TRUE;

    // Subject Alt Names
    san_entry.dwAltNameChoice = CERT_ALT_NAME_DNS_NAME;
    san_entry.pwszDNSName = (LPWSTR)L"localhost";

    san_info.cAltEntry = 1;
    san_info.rgAltEntry = &san_entry;

    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_ALTERNATE_NAME, &san_info,
                             CRYPT_ENCODE_ALLOC_FLAG, NULL, &ext[3].Value.pbData, &ext[3].Value.cbData)) {
        av_log(NULL, AV_LOG_ERROR, "CryptEncodeObjectEx for KU failed\n");
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    ext[3].pszObjId = (LPSTR)szOID_SUBJECT_ALT_NAME2;
    ext[3].fCritical = TRUE;

    exts.cExtension = 4;
    exts.rgExtension = ext;

    prov_info.pwszProvName = (LPWSTR)MS_KEY_STORAGE_PROVIDER;
    prov_info.pwszContainerName = (LPWSTR)FF_NCRYPT_TEMP_KEY_NAME;
    prov_info.dwFlags = CERT_SET_KEY_CONTEXT_PROP_ID;

    *crtctx = CertCreateSelfSignCertificate(*key, &subject, 0, &prov_info, &sig_alg, NULL, NULL, &exts);
    if (!*crtctx) {
        av_log(NULL, AV_LOG_ERROR, "CertCreateSelfSignCertificate failed: %lu\n", GetLastError());
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    NCryptFreeObject(provider);
    av_free(subject.pbData);
    for (int i = 0; i < FF_ARRAY_ELEMS(ext); i++)
        LocalFree(ext[i].Value.pbData);

    return 0;

fail:
    if (*crtctx)
        CertFreeCertificateContext(*crtctx);
    if (*key)
        if (NCryptDeleteKey(*key, NCRYPT_SILENT_FLAG) != ERROR_SUCCESS)
            NCryptFreeObject(*key);
    if (provider)
        NCryptFreeObject(provider);
    if (subject.pbData)
        av_free(subject.pbData);
    for (int i = 0; i < FF_ARRAY_ELEMS(ext); i++)
        if (ext[i].Value.pbData)
            LocalFree(ext[i].Value.pbData);

    *key = 0;
    *crtctx = NULL;

    return ret;
}

static int tls_export_key_cert(NCRYPT_KEY_HANDLE key, PCCERT_CONTEXT crtctx,
                               char *key_buf, size_t key_sz, char *cert_buf, size_t cert_sz, char **fingerprint)
{
    DWORD keysize = 0;
    char *keybuf = NULL;

    SECURITY_STATUS sspi_ret;
    int ret = 0;

    sspi_ret = NCryptExportKey(key, 0, NCRYPT_PKCS8_PRIVATE_KEY_BLOB, NULL, NULL, 0, &keysize, 0);
    if (sspi_ret != ERROR_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "Initial NCryptExportKey failed(0x%lx)\n", sspi_ret);
        ret = AVERROR_EXTERNAL;
        goto end;
    }

    keybuf = av_malloc(keysize);
    if (!keybuf) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    sspi_ret = NCryptExportKey(key, 0, NCRYPT_PKCS8_PRIVATE_KEY_BLOB, NULL, keybuf, keysize, &keysize, 0);
    if (sspi_ret != ERROR_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "Initial NCryptExportKey failed(0x%lx)\n", sspi_ret);
        ret = AVERROR_EXTERNAL;
        goto end;
    }

    ret = der_to_pem(keybuf, keysize, "PRIVATE KEY", key_buf, key_sz);
    if (ret < 0)
        goto end;

    ret = der_to_pem(crtctx->pbCertEncoded, crtctx->cbCertEncoded, "CERTIFICATE", cert_buf, cert_sz);
    if (ret < 0)
        goto end;

    ret = der_to_fingerprint(crtctx->pbCertEncoded, crtctx->cbCertEncoded, fingerprint);
    if (ret < 0)
        goto end;

end:
    av_free(keybuf);
    return ret;
}

int ff_ssl_gen_key_cert(char *key_buf, size_t key_sz, char *cert_buf, size_t cert_sz, char **fingerprint)
{
    NCRYPT_KEY_HANDLE key = 0;
    PCCERT_CONTEXT crtctx = NULL;

    int ret = tls_gen_self_signed(&key, &crtctx);
    if (ret < 0)
        goto end;

    ret = tls_export_key_cert(key, crtctx, key_buf, key_sz, cert_buf, cert_sz, fingerprint);
    if (ret < 0)
        goto end;

end:
    if (key)
        if (NCryptDeleteKey(key, NCRYPT_SILENT_FLAG) != ERROR_SUCCESS)
            NCryptFreeObject(key);
    if (crtctx)
        CertFreeCertificateContext(crtctx);

    return ret;
}

static int tls_import_key_cert(char *key_buf, char *cert_buf, NCRYPT_KEY_HANDLE *key, PCCERT_CONTEXT *crtctx)
{
    NCRYPT_PROV_HANDLE provider = 0;

    DWORD export_props = NCRYPT_ALLOW_EXPORT_FLAG | NCRYPT_ALLOW_PLAINTEXT_EXPORT_FLAG;
    DWORD usage_props = NCRYPT_ALLOW_ALL_USAGES;
    NCryptBufferDesc buffer_desc = { 0 };
    NCryptBuffer buffer = { 0 };
    CRYPT_KEY_PROV_INFO prov_info = { 0 };

    int key_der_len = 0, cert_der_len = 0;
    char *key_der = NULL, *cert_der = NULL;

    SECURITY_STATUS sspi_ret;
    int ret = 0;

    ret = pem_to_der(key_buf, &key_der, &key_der_len);
    if (ret < 0)
        goto fail;

    ret = pem_to_der(cert_buf, &cert_der, &cert_der_len);
    if (ret < 0)
        goto fail;

    sspi_ret = NCryptOpenStorageProvider(&provider, MS_KEY_STORAGE_PROVIDER, 0);
    if (sspi_ret != ERROR_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "NCryptOpenStorageProvider failed(0x%lx)\n", sspi_ret);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    buffer_desc.ulVersion = NCRYPTBUFFER_VERSION;
    buffer_desc.cBuffers = 1;
    buffer_desc.pBuffers = &buffer;

    buffer.BufferType = NCRYPTBUFFER_PKCS_KEY_NAME;
    buffer.pvBuffer = (LPWSTR)FF_NCRYPT_TEMP_KEY_NAME;
    buffer.cbBuffer = sizeof(FF_NCRYPT_TEMP_KEY_NAME);

    sspi_ret = NCryptImportKey(provider, 0, NCRYPT_PKCS8_PRIVATE_KEY_BLOB, &buffer_desc, key, key_der, key_der_len, NCRYPT_DO_NOT_FINALIZE_FLAG | NCRYPT_OVERWRITE_KEY_FLAG);
    if (sspi_ret != ERROR_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "NCryptImportKey failed(0x%lx)\n", sspi_ret);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    sspi_ret = NCryptSetProperty(*key, NCRYPT_EXPORT_POLICY_PROPERTY, (PBYTE)&export_props, sizeof(export_props), 0);
    if (sspi_ret != ERROR_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "NCryptSetProperty(NCRYPT_EXPORT_POLICY_PROPERTY) failed(0x%lx)\n", sspi_ret);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    sspi_ret = NCryptSetProperty(*key, NCRYPT_KEY_USAGE_PROPERTY, (PBYTE)&usage_props, sizeof(usage_props), 0);
    if (sspi_ret != ERROR_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "NCryptSetProperty(NCRYPT_KEY_USAGE_PROPERTY) failed(0x%lx)\n", sspi_ret);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    sspi_ret = NCryptFinalizeKey(*key, 0);
    if (sspi_ret != ERROR_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "NCryptFinalizeKey failed(0x%lx)\n", sspi_ret);
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    *crtctx = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, cert_der, cert_der_len);
    if (!*crtctx) {
        av_log(NULL, AV_LOG_ERROR, "CertCreateCertificateContext failed: %lu\n", GetLastError());
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    if (!CertSetCertificateContextProperty(*crtctx, CERT_NCRYPT_KEY_HANDLE_PROP_ID, 0, key)) {
        av_log(NULL, AV_LOG_ERROR, "CertSetCertificateContextProperty(CERT_NCRYPT_KEY_HANDLE_PROP_ID) failed: %lu\n", GetLastError());
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    prov_info.pwszProvName = (LPWSTR)MS_KEY_STORAGE_PROVIDER;
    prov_info.pwszContainerName = (LPWSTR)FF_NCRYPT_TEMP_KEY_NAME;
    prov_info.dwFlags = CERT_SET_KEY_CONTEXT_PROP_ID;

    if (!CertSetCertificateContextProperty(*crtctx, CERT_KEY_PROV_INFO_PROP_ID, 0, &prov_info)) {
        av_log(NULL, AV_LOG_ERROR, "CertSetCertificateContextProperty(CERT_KEY_PROV_INFO_PROP_ID) failed: %lu\n", GetLastError());
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    goto end;

fail:
    if (*key)
        if (NCryptDeleteKey(*key, NCRYPT_SILENT_FLAG) != ERROR_SUCCESS)
            NCryptFreeObject(*key);
    if (*crtctx)
        CertFreeCertificateContext(*crtctx);

    *key = 0;
    *crtctx = NULL;

end:
    if (key_der)
        av_free(key_der);
    if (cert_der)
        av_free(cert_der);
    if (provider)
        NCryptFreeObject(provider);
    return ret;
}

static int tls_cert_from_store(void *logctx, const char *cert_store_name, const char *cert_subj, PCCERT_CONTEXT *crtctx)
{
    HCERTSTORE cert_store = NULL;
    int ret = 0;

    cert_store = CertOpenStore(CERT_STORE_PROV_SYSTEM_A, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, cert_store_name);
    if (!cert_store) {
        av_log(logctx, AV_LOG_ERROR, "Opening user cert store %s failed\n", cert_store_name);
        ret = AVERROR_EXTERNAL;
        goto end;
    }

    *crtctx = CertFindCertificateInStore(cert_store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SUBJECT_STR_A, cert_subj, NULL);
    if (!*crtctx) {
        av_log(logctx, AV_LOG_ERROR, "Could not find certificate in store\n");
        ret = AVERROR_EXTERNAL;
        goto end;
    }

end:
    if (cert_store)
        CertCloseStore(cert_store, 0);

    return ret;
}

static int tls_load_key_cert(char *key_url, char *cert_url, NCRYPT_KEY_HANDLE *key, PCCERT_CONTEXT *crtctx)
{
    AVBPrint key_bp, cert_bp;
    int ret = 0;

    av_bprint_init(&key_bp, 1, MAX_CERTIFICATE_SIZE);
    av_bprint_init(&cert_bp, 1, MAX_CERTIFICATE_SIZE);

    /* Read key file. */
    ret = ff_url_read_all(key_url, &key_bp);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to open key file %s\n", key_url);
        goto end;
    }

    ret = ff_url_read_all(cert_url, &cert_bp);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to open cert file %s\n", cert_url);
        goto end;
    }

    ret = tls_import_key_cert(key_bp.str, cert_bp.str, key, crtctx);
    if (ret < 0)
        goto end;

end:
    av_bprint_finalize(&key_bp, NULL);
    av_bprint_finalize(&cert_bp, NULL);

    return ret;
}

int ff_ssl_read_key_cert(char *key_url, char *cert_url, char *key_buf, size_t key_sz, char *cert_buf, size_t cert_sz, char **fingerprint)
{
    NCRYPT_KEY_HANDLE key = 0;
    PCCERT_CONTEXT crtctx = NULL;

    int ret = tls_load_key_cert(key_url, cert_url, &key, &crtctx);
    if (ret < 0)
        goto end;

    ret = tls_export_key_cert(key, crtctx, key_buf, key_sz, cert_buf, cert_sz, fingerprint);
    if (ret < 0)
        goto end;

end:
    if (key)
        if (NCryptDeleteKey(key, NCRYPT_SILENT_FLAG) != ERROR_SUCCESS)
            NCryptFreeObject(key);
    if (crtctx)
        CertFreeCertificateContext(crtctx);

    return ret;
}

typedef struct TLSContext {
    const AVClass *class;
    TLSShared tls_shared;

    char *cert_store_subject;
    char *cert_store_name;

    CredHandle cred_handle;
    TimeStamp cred_timestamp;

    CtxtHandle ctxt_handle;
    int have_context;
    TimeStamp ctxt_timestamp;

    ULONG request_flags;
    ULONG context_flags;

    uint8_t *enc_buf;
    int enc_buf_size;
    int enc_buf_offset;

    uint8_t *dec_buf;
    int dec_buf_size;
    int dec_buf_offset;

    char *send_buf;
    int send_buf_size;
    int send_buf_offset;

    SecPkgContext_StreamSizes sizes;

    int connected;
    int connection_closed;
    int sspi_close_notify;
} TLSContext;

int ff_tls_set_external_socket(URLContext *h, URLContext *sock)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;

    if (s->is_dtls)
        c->tls_shared.udp = sock;
    else
        c->tls_shared.tcp = sock;

    return 0;
}

int ff_dtls_export_materials(URLContext *h, char *dtls_srtp_materials, size_t materials_sz)
{
#if HAVE_SECPKGCONTEXT_KEYINGMATERIALINFO
    TLSContext *c = h->priv_data;

    SecPkgContext_KeyingMaterialInfo keying_info = { 0 };
    SecPkgContext_KeyingMaterial keying_material = { 0 };

    const char* dst = "EXTRACTOR-dtls_srtp";
    SECURITY_STATUS sspi_ret;

    if (!c->have_context)
        return AVERROR(EINVAL);

    keying_info.cbLabel = strlen(dst) + 1;
    keying_info.pszLabel = (LPSTR)dst;
    keying_info.cbContextValue = 0;
    keying_info.pbContextValue = NULL;
    keying_info.cbKeyingMaterial = materials_sz;

    sspi_ret = SetContextAttributes(&c->ctxt_handle, SECPKG_ATTR_KEYING_MATERIAL_INFO, &keying_info, sizeof(keying_info));
    if (sspi_ret != SEC_E_OK) {
        av_log(h, AV_LOG_ERROR, "Setting keying material info failed: %lx\n", sspi_ret);
        return AVERROR_EXTERNAL;
    }

    sspi_ret = QueryContextAttributes(&c->ctxt_handle, SECPKG_ATTR_KEYING_MATERIAL, &keying_material);
    if (sspi_ret != SEC_E_OK) {
        av_log(h, AV_LOG_ERROR, "Querying keying material failed: %lx\n", sspi_ret);
        return AVERROR_EXTERNAL;
    }

    memcpy(dtls_srtp_materials, keying_material.pbKeyingMaterial, FFMIN(materials_sz, keying_material.cbKeyingMaterial));
    FreeContextBuffer(keying_material.pbKeyingMaterial);

    if (keying_material.cbKeyingMaterial > materials_sz) {
        av_log(h, AV_LOG_WARNING, "Keying material size mismatch: %ld > %zu\n", keying_material.cbKeyingMaterial, materials_sz);
        return AVERROR(ENOSPC);
    }

    return 0;
#else
    return AVERROR(ENOSYS);
#endif
}

int ff_dtls_state(URLContext *h)
{
    TLSContext *c = h->priv_data;
    return c->tls_shared.state;
}

static void init_sec_buffer(SecBuffer *buffer, unsigned long type,
                            void *data, unsigned long size)
{
    buffer->cbBuffer   = size;
    buffer->BufferType = type;
    buffer->pvBuffer   = data;
}

static void init_sec_buffer_desc(SecBufferDesc *desc, SecBuffer *buffers,
                                 unsigned long buffer_count)
{
    desc->ulVersion = SECBUFFER_VERSION;
    desc->pBuffers = buffers;
    desc->cBuffers = buffer_count;
}

static int tls_process_send_buffer(URLContext *h)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    URLContext *uc = s->is_dtls ? s->udp : s->tcp;
    int ret;

    if (!c->send_buf)
        return 0;

    ret = ffurl_write(uc, c->send_buf + c->send_buf_offset, c->send_buf_size - c->send_buf_offset);
    if (ret == AVERROR(EAGAIN)) {
        return AVERROR(EAGAIN);
    } else if (ret < 0) {
        av_log(h, AV_LOG_ERROR, "Writing encrypted data to socket failed\n");
        return AVERROR(EIO);
    }

    c->send_buf_offset += ret;

    if (c->send_buf_offset < c->send_buf_size)
        return AVERROR(EAGAIN);

    av_freep(&c->send_buf);
    c->send_buf_size = c->send_buf_offset = 0;

    return 0;
}

static int tls_shutdown_client(URLContext *h)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    URLContext *uc = s->is_dtls ? s->udp : s->tcp;
    int ret;

    if (c->connected) {
        SecBufferDesc BuffDesc;
        SecBuffer Buffer;
        SECURITY_STATUS sspi_ret;
        SecBuffer outbuf;
        SecBufferDesc outbuf_desc;

        DWORD dwshut = SCHANNEL_SHUTDOWN;
        init_sec_buffer(&Buffer, SECBUFFER_TOKEN, &dwshut, sizeof(dwshut));
        init_sec_buffer_desc(&BuffDesc, &Buffer, 1);

        uc->flags &= ~AVIO_FLAG_NONBLOCK;
        ret = tls_process_send_buffer(h);
        if (ret < 0)
            return ret;

        sspi_ret = ApplyControlToken(&c->ctxt_handle, &BuffDesc);
        if (sspi_ret != SEC_E_OK)
            av_log(h, AV_LOG_ERROR, "ApplyControlToken failed\n");

        init_sec_buffer(&outbuf, SECBUFFER_TOKEN, NULL, 0);
        init_sec_buffer_desc(&outbuf_desc, &outbuf, 1);

        do {
            if (s->listen)
                sspi_ret = AcceptSecurityContext(&c->cred_handle, &c->ctxt_handle, NULL, c->request_flags, 0,
                                                 &c->ctxt_handle, &outbuf_desc, &c->context_flags,
                                                 &c->ctxt_timestamp);
            else
                sspi_ret = InitializeSecurityContext(&c->cred_handle, &c->ctxt_handle, s->host,
                                                     c->request_flags, 0, 0, NULL, 0, &c->ctxt_handle,
                                                     &outbuf_desc, &c->context_flags, &c->ctxt_timestamp);

            if (outbuf.pvBuffer) {
                if (outbuf.cbBuffer > 0) {
                    ret = ffurl_write(uc, outbuf.pvBuffer, outbuf.cbBuffer);
                    if (ret < 0 || ret != outbuf.cbBuffer)
                        av_log(h, AV_LOG_ERROR, "Failed to send close message\n");
                }
                FreeContextBuffer(outbuf.pvBuffer);
            }
        } while(
#ifdef SEC_I_MESSAGE_FRAGMENT
                sspi_ret == SEC_I_MESSAGE_FRAGMENT ||
#endif
                sspi_ret == SEC_I_CONTINUE_NEEDED);

        av_log(h, AV_LOG_DEBUG, "Close session result: 0x%lx\n", sspi_ret);

        c->connected = 0;
    }
    return 0;
}

static int tls_close(URLContext *h)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;

    tls_shutdown_client(h);

    DeleteSecurityContext(&c->ctxt_handle);
    FreeCredentialsHandle(&c->cred_handle);

    av_freep(&c->enc_buf);
    c->enc_buf_size = c->enc_buf_offset = 0;

    av_freep(&c->dec_buf);
    c->dec_buf_size = c->dec_buf_offset = 0;

    av_freep(&c->send_buf);
    c->send_buf_size = c->send_buf_offset = 0;

    if (s->is_dtls) {
        if (!s->external_sock)
            ffurl_closep(&c->tls_shared.udp);
    } else {
        ffurl_closep(&c->tls_shared.tcp);
    }

    return 0;
}

static int tls_handshake_loop(URLContext *h, int initial)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    URLContext *uc = s->is_dtls ? s->udp : s->tcp;
    SECURITY_STATUS sspi_ret;
    SecBuffer outbuf[3] = { 0 };
    SecBufferDesc outbuf_desc;
    SecBuffer inbuf[3];
    SecBufferDesc inbuf_desc;
    struct sockaddr_storage recv_addr = { 0 };
    socklen_t recv_addr_len = 0;
    int i, ret = 0, read_data = initial;

    if (c->enc_buf == NULL) {
        c->enc_buf_offset = 0;
        ret = av_reallocp(&c->enc_buf, SCHANNEL_INITIAL_BUFFER_SIZE);
        if (ret < 0)
            goto fail;
        c->enc_buf_size = SCHANNEL_INITIAL_BUFFER_SIZE;
    }

    if (c->dec_buf == NULL) {
        c->dec_buf_offset = 0;
        ret = av_reallocp(&c->dec_buf, SCHANNEL_INITIAL_BUFFER_SIZE);
        if (ret < 0)
            goto fail;
        c->dec_buf_size = SCHANNEL_INITIAL_BUFFER_SIZE;
    }

    uc->flags &= ~AVIO_FLAG_NONBLOCK;

    while (1) {
        if (c->enc_buf_size - c->enc_buf_offset < SCHANNEL_FREE_BUFFER_SIZE) {
            c->enc_buf_size = c->enc_buf_offset + SCHANNEL_FREE_BUFFER_SIZE;
            ret = av_reallocp(&c->enc_buf, c->enc_buf_size);
            if (ret < 0) {
                c->enc_buf_size = c->enc_buf_offset = 0;
                goto fail;
            }
        }

        if (read_data) {
            ret = ffurl_read(uc, c->enc_buf + c->enc_buf_offset, c->enc_buf_size - c->enc_buf_offset);
            if (ret < 0) {
                av_log(h, AV_LOG_ERROR, "Failed to read handshake response\n");
                goto fail;
            }
            c->enc_buf_offset += ret;
            if (s->is_dtls && !recv_addr_len) {
                ff_udp_get_last_recv_addr(uc, &recv_addr, &recv_addr_len);

                if (s->listen) {
                    ret = ff_udp_set_remote_addr(uc, (struct sockaddr *)&recv_addr, recv_addr_len, 1);
                    if (ret < 0) {
                        av_log(h, AV_LOG_ERROR, "Failed connecting udp context\n");
                        goto fail;
                    }
                    av_log(h, AV_LOG_TRACE, "Set UDP remote addr on UDP socket, now 'connected'\n");
                }
            }
        }

        /* input buffers */
        init_sec_buffer(&inbuf[0], SECBUFFER_TOKEN, av_malloc(c->enc_buf_offset), c->enc_buf_offset);
        init_sec_buffer(&inbuf[1], SECBUFFER_EMPTY, NULL, 0);
        if (s->listen && s->is_dtls) {
            init_sec_buffer(&inbuf[2], SECBUFFER_EXTRA, &recv_addr, recv_addr_len);
            init_sec_buffer_desc(&inbuf_desc, inbuf, 3);
        } else {
            init_sec_buffer_desc(&inbuf_desc, inbuf, 2);
        }

        if (inbuf[0].pvBuffer == NULL) {
            av_log(h, AV_LOG_ERROR, "Failed to allocate input buffer\n");
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        memcpy(inbuf[0].pvBuffer, c->enc_buf, c->enc_buf_offset);

        /* output buffers */
        init_sec_buffer(&outbuf[0], SECBUFFER_TOKEN, NULL, 0);
        init_sec_buffer(&outbuf[1], SECBUFFER_ALERT, NULL, 0);
        init_sec_buffer(&outbuf[2], SECBUFFER_EMPTY, NULL, 0);
        init_sec_buffer_desc(&outbuf_desc, outbuf, 3);

        if (s->listen)
            sspi_ret = AcceptSecurityContext(&c->cred_handle, c->have_context ? &c->ctxt_handle : NULL, &inbuf_desc,
                                             c->request_flags, 0, &c->ctxt_handle, &outbuf_desc,
                                             &c->context_flags, &c->ctxt_timestamp);
        else
            sspi_ret = InitializeSecurityContext(&c->cred_handle, c->have_context ? &c->ctxt_handle : NULL,
                                                 s->host, c->request_flags, 0, 0, &inbuf_desc, 0, &c->ctxt_handle,
                                                 &outbuf_desc, &c->context_flags, &c->ctxt_timestamp);
        av_freep(&inbuf[0].pvBuffer);

        av_log(h, AV_LOG_TRACE, "Handshake res with %d bytes of data: 0x%lx\n", c->enc_buf_offset, sspi_ret);

        if (sspi_ret == SEC_E_INCOMPLETE_MESSAGE) {
            av_log(h, AV_LOG_TRACE, "Received incomplete handshake, need more data\n");
            read_data = 1;
            continue;
        }

        c->have_context = 1;

        /* remote requests a client certificate - attempt to continue without one anyway */
        if (sspi_ret == SEC_I_INCOMPLETE_CREDENTIALS &&
            !(c->request_flags & ISC_REQ_USE_SUPPLIED_CREDS)) {
            av_log(h, AV_LOG_VERBOSE, "Client certificate has been requested, ignoring\n");
            c->request_flags |= ISC_REQ_USE_SUPPLIED_CREDS;
            read_data = 0;
            continue;
        }

        /* continue handshake */
        if (sspi_ret == SEC_I_CONTINUE_NEEDED ||
#ifdef SEC_I_MESSAGE_FRAGMENT
            sspi_ret == SEC_I_MESSAGE_FRAGMENT ||
#endif
            sspi_ret == SEC_E_OK) {
            for (i = 0; i < 3; i++) {
                if (outbuf[i].BufferType == SECBUFFER_TOKEN && outbuf[i].cbBuffer > 0) {
                    ret = ffurl_write(uc, outbuf[i].pvBuffer, outbuf[i].cbBuffer);
                    if (ret < 0 || ret != outbuf[i].cbBuffer) {
                        av_log(h, AV_LOG_VERBOSE, "Failed to send handshake data\n");
                        ret = AVERROR(EIO);
                        goto fail;
                    }
                }

                if (outbuf[i].pvBuffer != NULL) {
                    FreeContextBuffer(outbuf[i].pvBuffer);
                    outbuf[i].pvBuffer = NULL;
                }
            }
        } else {
            if (sspi_ret == SEC_E_WRONG_PRINCIPAL)
                av_log(h, AV_LOG_ERROR, "SNI or certificate check failed\n");
            else
                av_log(h, AV_LOG_ERROR, "Creating security context failed (0x%lx)\n", sspi_ret);
            ret = AVERROR_UNKNOWN;
            goto fail;
        }

#ifdef SEC_I_MESSAGE_FRAGMENT
        if (sspi_ret == SEC_I_MESSAGE_FRAGMENT) {
            av_log(h, AV_LOG_TRACE, "Writing fragmented output message part\n");
            read_data = 0;
            continue;
        }
#endif

        if (inbuf[1].BufferType == SECBUFFER_EXTRA && inbuf[1].cbBuffer > 0) {
            if (c->enc_buf_offset > inbuf[1].cbBuffer) {
                memmove(c->enc_buf, (c->enc_buf + c->enc_buf_offset) - inbuf[1].cbBuffer,
                        inbuf[1].cbBuffer);
                c->enc_buf_offset = inbuf[1].cbBuffer;
                if (sspi_ret == SEC_I_CONTINUE_NEEDED) {
                    av_log(h, AV_LOG_TRACE, "Sent reply, handshake continues. %d extra bytes\n", (int)inbuf[1].cbBuffer);
                    read_data = 0;
                    continue;
                }
            }
        } else {
            c->enc_buf_offset  = 0;
        }

        if (sspi_ret == SEC_I_CONTINUE_NEEDED) {
            av_log(h, AV_LOG_TRACE, "Handshake continues\n");
            read_data = 1;
            continue;
        }

        break;
    }

    av_log(h, AV_LOG_TRACE, "Handshake completed\n");

    return 0;

fail:
    /* free any remaining output data */
    for (i = 0; i < 3; i++) {
        if (outbuf[i].pvBuffer != NULL) {
            FreeContextBuffer(outbuf[i].pvBuffer);
            outbuf[i].pvBuffer = NULL;
        }
    }

    av_log(h, AV_LOG_TRACE, "Handshake failed\n");

    return ret;
}

static int tls_client_handshake(URLContext *h)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    URLContext *uc = s->is_dtls ? s->udp : s->tcp;
    SecBuffer outbuf;
    SecBufferDesc outbuf_desc;
    SECURITY_STATUS sspi_ret;
    int ret;

    init_sec_buffer(&outbuf, SECBUFFER_EMPTY, NULL, 0);
    init_sec_buffer_desc(&outbuf_desc, &outbuf, 1);

    c->request_flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                       ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY;
    if (s->is_dtls)
        c->request_flags |= ISC_REQ_DATAGRAM;
    else
        c->request_flags |= ISC_REQ_STREAM;

    sspi_ret = InitializeSecurityContext(&c->cred_handle, NULL, s->host, c->request_flags, 0, 0,
                                         NULL, 0, &c->ctxt_handle, &outbuf_desc, &c->context_flags,
                                         &c->ctxt_timestamp);
    if (sspi_ret != SEC_I_CONTINUE_NEEDED) {
        av_log(h, AV_LOG_ERROR, "Unable to create initial security context (0x%lx)\n", sspi_ret);
        ret = AVERROR_UNKNOWN;
        goto fail;
    }

    c->have_context = 1;

    uc->flags &= ~AVIO_FLAG_NONBLOCK;
    ret = ffurl_write(uc, outbuf.pvBuffer, outbuf.cbBuffer);
    FreeContextBuffer(outbuf.pvBuffer);
    if (ret < 0 || ret != outbuf.cbBuffer) {
        av_log(h, AV_LOG_ERROR, "Failed to send initial handshake data\n");
        ret = AVERROR(EIO);
        goto fail;
    }

    return tls_handshake_loop(h, 1);

fail:
    DeleteSecurityContext(&c->ctxt_handle);
    return ret;
}

static int tls_server_handshake(URLContext *h)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;

    c->request_flags = ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT |
                       ASC_REQ_CONFIDENTIALITY | ASC_REQ_ALLOCATE_MEMORY;
    if (s->is_dtls)
        c->request_flags |= ASC_REQ_DATAGRAM;
    else
        c->request_flags |= ASC_REQ_STREAM;

    c->have_context = 0;

    return tls_handshake_loop(h, 1);
}

static int tls_handshake(URLContext *h)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    SECURITY_STATUS sspi_ret;
    int ret = 0;

    if (s->listen)
        ret = tls_server_handshake(h);
    else
        ret = tls_client_handshake(h);

    if (ret < 0)
        goto fail;

#if CONFIG_DTLS_PROTOCOL
    if (s->is_dtls && s->mtu > 0) {
        ULONG mtu = s->mtu;
        sspi_ret = SetContextAttributes(&c->ctxt_handle, SECPKG_ATTR_DTLS_MTU, &mtu, sizeof(mtu));
        if (sspi_ret != SEC_E_OK) {
            av_log(h, AV_LOG_ERROR, "Failed setting DTLS MTU to %d.\n", s->mtu);
            ret = AVERROR(EINVAL);
            goto fail;
        }
        av_log(h, AV_LOG_VERBOSE, "Set DTLS MTU to %d\n", s->mtu);
    }
#endif

    c->connected = 1;
    s->state = DTLS_STATE_FINISHED;

fail:
    return ret;
}

static int tls_open(URLContext *h, const char *uri, int flags, AVDictionary **options)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    SECURITY_STATUS sspi_ret;
    SCHANNEL_CRED schannel_cred = { 0 };
    PCCERT_CONTEXT crtctx = NULL;
    NCRYPT_KEY_HANDLE key = 0;
    int ret = 0;

    if (!s->external_sock) {
        if ((ret = ff_tls_open_underlying(s, h, uri, options)) < 0)
            goto fail;
    }

    /* SChannel Options */
    schannel_cred.dwVersion = SCHANNEL_CRED_VERSION;

    if (s->listen) {
        if (c->cert_store_name && c->cert_store_subject) {
            ret = tls_cert_from_store(h, c->cert_store_name, c->cert_store_subject, &crtctx);
        } else if (s->key_buf && s->cert_buf) {
            ret = tls_import_key_cert(s->key_buf, s->cert_buf, &key, &crtctx);
        } else if (s->key_file && s->cert_file) {
            ret = tls_load_key_cert(s->key_file, s->cert_file, &key, &crtctx);
        } else {
            av_log(h, AV_LOG_VERBOSE, "No server certificate provided, using self-signed\n");
            ret = tls_gen_self_signed(&key, &crtctx);
        }

        if (ret < 0)
            goto fail;

        schannel_cred.cCreds = 1;
        schannel_cred.paCred = &crtctx;

        schannel_cred.dwFlags = SCH_CRED_NO_SYSTEM_MAPPER | SCH_CRED_MANUAL_CRED_VALIDATION;

#if CONFIG_DTLS_PROTOCOL
        if (s->is_dtls)
            schannel_cred.grbitEnabledProtocols = SP_PROT_DTLS1_X_SERVER;
#endif
    } else {
        if (s->verify)
            schannel_cred.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION |
                                    SCH_CRED_REVOCATION_CHECK_CHAIN;
        else
            schannel_cred.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION |
                                    SCH_CRED_IGNORE_NO_REVOCATION_CHECK |
                                    SCH_CRED_IGNORE_REVOCATION_OFFLINE;

#if CONFIG_DTLS_PROTOCOL
        if (s->is_dtls)
            schannel_cred.grbitEnabledProtocols = SP_PROT_DTLS1_X_CLIENT;
#endif
    }

    /* Get credential handle */
    sspi_ret = AcquireCredentialsHandle(NULL, (TCHAR *)UNISP_NAME,
                                        s->listen ? SECPKG_CRED_INBOUND : SECPKG_CRED_OUTBOUND,
                                        NULL, &schannel_cred, NULL, NULL, &c->cred_handle,
                                        &c->cred_timestamp);
    if (sspi_ret != SEC_E_OK) {
        av_log(h, AV_LOG_ERROR, "Unable to acquire security credentials (0x%lx)\n", sspi_ret);
        ret = AVERROR_UNKNOWN;
        goto fail;
    }

    if (!s->external_sock) {
        ret = tls_handshake(h);
        if (ret < 0)
            goto fail;
    }

    goto end;

fail:
    tls_close(h);

end:
    if (crtctx)
        CertFreeCertificateContext(crtctx);
    if (key)
        if (NCryptDeleteKey(key, NCRYPT_SILENT_FLAG) != ERROR_SUCCESS)
            NCryptFreeObject(key);

    return ret;
}

#if CONFIG_DTLS_PROTOCOL
static int dtls_open(URLContext *h, const char *uri, int flags, AVDictionary **options)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;

    s->is_dtls = 1;

    return tls_open(h, uri, flags, options);
}
#endif

static int tls_read(URLContext *h, uint8_t *buf, int len)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    URLContext *uc = s->is_dtls ? s->udp : s->tcp;
    SECURITY_STATUS sspi_ret = SEC_E_OK;
    SecBuffer inbuf[4];
    SecBufferDesc inbuf_desc;
    int size, ret = 0;
    int min_enc_buf_size = len + SCHANNEL_FREE_BUFFER_SIZE;

    /* If we have some left-over data from previous network activity,
     * return it first in case it is enough. It may contain
     * data that is required to know whether this connection
     * is still required or not, esp. in case of HTTP keep-alive
     * connections. */
    if (c->dec_buf_offset > 0)
        goto cleanup;

    if (c->sspi_close_notify)
        goto cleanup;

    if (!c->connection_closed) {
        size = c->enc_buf_size - c->enc_buf_offset;
        if (size < SCHANNEL_FREE_BUFFER_SIZE || c->enc_buf_size < min_enc_buf_size) {
            c->enc_buf_size = c->enc_buf_offset + SCHANNEL_FREE_BUFFER_SIZE;
            if (c->enc_buf_size < min_enc_buf_size)
                c->enc_buf_size = min_enc_buf_size;
            ret = av_reallocp(&c->enc_buf, c->enc_buf_size);
            if (ret < 0) {
                c->enc_buf_size = c->enc_buf_offset = 0;
                return ret;
            }
        }

        uc->flags &= ~AVIO_FLAG_NONBLOCK;
        uc->flags |= h->flags & AVIO_FLAG_NONBLOCK;

        ret = ffurl_read(uc, c->enc_buf + c->enc_buf_offset,
                         c->enc_buf_size - c->enc_buf_offset);
        if (ret == AVERROR_EOF) {
            c->connection_closed = 1;
            ret = 0;
        } else if (ret == AVERROR(EAGAIN)) {
            ret = 0;
        } else if (ret < 0) {
            av_log(h, AV_LOG_ERROR, "Unable to read from socket\n");
            return ret;
        }

        c->enc_buf_offset += ret;
    }

    while (c->enc_buf_offset > 0 && sspi_ret == SEC_E_OK) {
        /*  input buffer */
        init_sec_buffer(&inbuf[0], SECBUFFER_DATA, c->enc_buf, c->enc_buf_offset);

        /* additional buffers for possible output */
        init_sec_buffer(&inbuf[1], SECBUFFER_EMPTY, NULL, 0);
        init_sec_buffer(&inbuf[2], SECBUFFER_EMPTY, NULL, 0);
        init_sec_buffer(&inbuf[3], SECBUFFER_EMPTY, NULL, 0);
        init_sec_buffer_desc(&inbuf_desc, inbuf, 4);

        sspi_ret = DecryptMessage(&c->ctxt_handle, &inbuf_desc, 0, NULL);
        if (sspi_ret == SEC_E_OK || sspi_ret == SEC_I_RENEGOTIATE ||
            sspi_ret == SEC_I_CONTEXT_EXPIRED) {
            /* handle decrypted data */
            if (inbuf[1].BufferType == SECBUFFER_DATA) {
                /* grow buffer if needed */
                size = inbuf[1].cbBuffer > SCHANNEL_FREE_BUFFER_SIZE ?
                       inbuf[1].cbBuffer : SCHANNEL_FREE_BUFFER_SIZE;
                if (c->dec_buf_size - c->dec_buf_offset < size || c->dec_buf_size < len)  {
                    c->dec_buf_size = c->dec_buf_offset + size;
                    if (c->dec_buf_size < len)
                        c->dec_buf_size = len;
                    ret = av_reallocp(&c->dec_buf, c->dec_buf_size);
                    if (ret < 0) {
                        c->dec_buf_size = c->dec_buf_offset = 0;
                        return ret;
                    }
                }

                /* copy decrypted data to buffer */
                size = inbuf[1].cbBuffer;
                if (size) {
                    memcpy(c->dec_buf + c->dec_buf_offset, inbuf[1].pvBuffer, size);
                    c->dec_buf_offset += size;
                }
            }
            if (inbuf[3].BufferType == SECBUFFER_EXTRA && inbuf[3].cbBuffer > 0) {
                if (c->enc_buf_offset > inbuf[3].cbBuffer) {
                    memmove(c->enc_buf, (c->enc_buf + c->enc_buf_offset) - inbuf[3].cbBuffer,
                    inbuf[3].cbBuffer);
                    c->enc_buf_offset = inbuf[3].cbBuffer;
                }
            } else
                c->enc_buf_offset = 0;

            if (sspi_ret == SEC_I_RENEGOTIATE) {
                if (c->enc_buf_offset) {
                    av_log(h, AV_LOG_ERROR, "Cannot renegotiate, encrypted data buffer not empty\n");
                    ret = AVERROR_UNKNOWN;
                    goto cleanup;
                }

                av_log(h, AV_LOG_VERBOSE, "Re-negotiating security context\n");
                ret = tls_handshake_loop(h, 0);
                if (ret < 0) {
                    goto cleanup;
                }
                sspi_ret = SEC_E_OK;

                /* if somehow any send data was left, it is now invalid */
                av_freep(&c->send_buf);
                c->send_buf_size = c->send_buf_offset = 0;

                continue;
            } else if (sspi_ret == SEC_I_CONTEXT_EXPIRED) {
                c->sspi_close_notify = 1;
                if (!c->connection_closed) {
                    c->connection_closed = 1;
                    av_log(h, AV_LOG_VERBOSE, "Server closed the connection\n");
                }
                ret = 0;
                goto cleanup;
            }
        } else if (sspi_ret == SEC_E_INCOMPLETE_MESSAGE) {
            ret = AVERROR(EAGAIN);
            goto cleanup;
        } else {
            av_log(h, AV_LOG_ERROR, "Unable to decrypt message (error 0x%x)\n", (unsigned)sspi_ret);
            ret = AVERROR(EIO);
            goto cleanup;
        }
    }

    ret = 0;

cleanup:
    size = FFMIN(len, c->dec_buf_offset);
    if (size) {
        memcpy(buf, c->dec_buf, size);
        memmove(c->dec_buf, c->dec_buf + size, c->dec_buf_offset - size);
        c->dec_buf_offset -= size;

        return size;
    }

    if (ret == 0 && !c->connection_closed)
        ret = AVERROR(EAGAIN);

    return ret < 0 ? ret : AVERROR_EOF;
}

static int tls_write(URLContext *h, const uint8_t *buf, int len)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    URLContext *uc = s->is_dtls ? s->udp : s->tcp;
    SECURITY_STATUS sspi_ret;
    SecBuffer outbuf[4];
    SecBufferDesc outbuf_desc;
    int ret = 0;

    uc->flags &= ~AVIO_FLAG_NONBLOCK;
    uc->flags |= h->flags & AVIO_FLAG_NONBLOCK;

    ret = tls_process_send_buffer(h);
    if (ret < 0)
        return ret;

    if (c->sizes.cbMaximumMessage == 0) {
        sspi_ret = QueryContextAttributes(&c->ctxt_handle, SECPKG_ATTR_STREAM_SIZES, &c->sizes);
        if (sspi_ret != SEC_E_OK)
            return AVERROR_UNKNOWN;
    }

    /* limit how much data we can consume */
    len = FFMIN(len, c->sizes.cbMaximumMessage - c->sizes.cbHeader - c->sizes.cbTrailer);

    c->send_buf_size = c->sizes.cbHeader + len + c->sizes.cbTrailer;
    c->send_buf = av_malloc(c->send_buf_size);
    if (c->send_buf == NULL)
        return AVERROR(ENOMEM);

    init_sec_buffer(&outbuf[0], SECBUFFER_STREAM_HEADER,
                    c->send_buf, c->sizes.cbHeader);
    init_sec_buffer(&outbuf[1], SECBUFFER_DATA,
                    c->send_buf + c->sizes.cbHeader, len);
    init_sec_buffer(&outbuf[2], SECBUFFER_STREAM_TRAILER,
                    c->send_buf + c->sizes.cbHeader + len,
                    c->sizes.cbTrailer);
    init_sec_buffer(&outbuf[3], SECBUFFER_EMPTY, NULL, 0);
    init_sec_buffer_desc(&outbuf_desc, outbuf, 4);

    memcpy(outbuf[1].pvBuffer, buf, len);

    sspi_ret = EncryptMessage(&c->ctxt_handle, 0, &outbuf_desc, 0);
    if (sspi_ret != SEC_E_OK) {
        av_freep(&c->send_buf);
        av_log(h, AV_LOG_ERROR, "Encrypting data failed\n");
        if (sspi_ret == SEC_E_INSUFFICIENT_MEMORY)
            return AVERROR(ENOMEM);
        return AVERROR(EIO);
    }

    c->send_buf_size = outbuf[0].cbBuffer + outbuf[1].cbBuffer + outbuf[2].cbBuffer;
    c->send_buf_offset = 0;

    ret = tls_process_send_buffer(h);
    if (ret == AVERROR(EAGAIN)) {
         /* We always need to signal that we consumed all (encrypted) data since schannel must not
            be fed the same data again. Sending will then be completed next call to this function,
            and EAGAIN returned until all remaining buffer is sent. */
        return outbuf[1].cbBuffer;
    } else if (ret < 0) {
        return ret;
    }

    return outbuf[1].cbBuffer;
}

static int tls_get_file_handle(URLContext *h)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    return ffurl_get_file_handle(s->is_dtls ? s->udp : s->tcp);
}

static int tls_get_short_seek(URLContext *h)
{
    TLSContext *c = h->priv_data;
    TLSShared *s = &c->tls_shared;
    return ffurl_get_short_seek(s->is_dtls ? s->udp : s->tcp);
}

#define OFFSET(x) offsetof(TLSContext, x)
static const AVOption options[] = {
    TLS_COMMON_OPTIONS(TLSContext, tls_shared),
    { "cert_store_subject", "Load certificate (and associated key) from users keystore by subject",
                            OFFSET(cert_store_subject), AV_OPT_TYPE_STRING, .flags = TLS_OPTFL },
    { "cert_store_name",    "Name of the specific cert store to search in (for cert_store_subject)",
                            OFFSET(cert_store_name), AV_OPT_TYPE_STRING, { .str = "MY" }, .flags = TLS_OPTFL },
    { NULL }
};

#if CONFIG_TLS_PROTOCOL
static const AVClass tls_class = {
    .class_name = "tls",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_tls_protocol = {
    .name           = "tls",
    .url_open2      = tls_open,
    .url_read       = tls_read,
    .url_write      = tls_write,
    .url_close      = tls_close,
    .url_get_file_handle = tls_get_file_handle,
    .url_get_short_seek  = tls_get_short_seek,
    .priv_data_size = sizeof(TLSContext),
    .flags          = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class = &tls_class,
};
#endif

#if CONFIG_DTLS_PROTOCOL
static const AVClass dtls_class = {
    .class_name = "dtls",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_dtls_protocol = {
    .name           = "dtls",
    .url_open2      = dtls_open,
    .url_handshake  = tls_handshake,
    .url_close      = tls_close,
    .url_read       = tls_read,
    .url_write      = tls_write,
    .url_get_file_handle = tls_get_file_handle,
    .url_get_short_seek  = tls_get_short_seek,
    .priv_data_size = sizeof(TLSContext),
    .flags          = URL_PROTOCOL_FLAG_NETWORK,
    .priv_data_class = &dtls_class,
};
#endif
