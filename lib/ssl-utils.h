/**
 * @file ssl-utils.h
 */
#ifndef QJSNET_LIB_SSL_UTILS_H
#define QJSNET_LIB_SSL_UTILS_H

#include <openssl/bio.h>
#include <quickjs.h>
#include <cutils.h>

JSValue js_asn1_time(JSContext*, ASN1_TIME*);
JSValue js_x509_name(JSContext*, X509_NAME*);
void js_cert_object(JSContext*, JSValueConst, X509*);

BIO* ssl_bio_dynbuf_new(void);
void ssl_bio_dynbuf_clear(BIO*);
char* ssl_bio_dynbuf_string(BIO*);
void* ssl_bio_dynbuf_get(BIO*, size_t*);
JSValue ssl_bio_dynbuf_jsstring(BIO*, JSContext*);
JSValue ssl_bio_dynbuf_jsarraybuffer(BIO*, JSContext*);

BIO* ssl_bio_writebuf_new(void);
void ssl_bio_writebuf_clear(BIO*);
char* ssl_bio_writebuf_string(BIO*);
void* ssl_bio_writebuf_get(BIO*, size_t*);
JSValue ssl_bio_writebuf_jsstring(BIO*, JSContext*);
JSValue ssl_bio_writebuf_jsarraybuffer(BIO*, JSContext*);

BIO* ssl_bio_readbuf_new(const void*, size_t);

#endif /* QJSNET_LIB_SSL_UTILS_H */
