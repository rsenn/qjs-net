#include "js-utils.h"
#include "ssl-utils.h"
#include "buffer.h"
#include <stdio.h>
#include <string.h>
#include <openssl/x509.h>

static BIO_METHOD* bio_dynbuf;

JSValue
js_asn1_time(JSContext* ctx, ASN1_TIME* at) {
  char buf[64];
  struct tm t;

  ASN1_TIME_to_tm(at, &t);
  snprintf(
      buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.000+%02d:%02d", t.tm_year + 1900, t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, (int)(t.tm_gmtoff / 3600), (int)((t.tm_gmtoff / 60) % 60));

  return js_date_from_str(ctx, buf);
}

JSValue
js_asn1_integer(JSContext* ctx, ASN1_INTEGER* ai) {
  int64_t i64 = -1;
  ASN1_INTEGER_get_int64(&i64, ai);
  return JS_NewInt64(ctx, i64);
}

JSValue
js_x509_name(JSContext* ctx, X509_NAME* name) {
  BIO* bio = ssl_bio_dynbuf_new();

  X509_NAME_print(bio, name, 10);
  JSValue ret = ssl_bio_dynbuf_jsstring(bio, ctx);
  BIO_free(bio);

  return ret;
}

void
js_cert_object(JSContext* ctx, JSValueConst obj, X509* cert) {
  BIO* bio = ssl_bio_dynbuf_new();
  X509_PUBKEY* pubkey2 = X509_get_X509_PUBKEY(cert);
  EVP_PKEY* pubkey = X509_PUBKEY_get(pubkey2);
  ASN1_PCTX* ap = ASN1_PCTX_new();
  EVP_PKEY_print_public(bio, pubkey, 4, ap);
  ASN1_PCTX_free(ap);

  JS_SetPropertyStr(ctx, obj, "pubKey", ssl_bio_dynbuf_jsstring(bio, ctx));
  ssl_bio_dynbuf_clear(bio);

  X509_print(bio, cert);
  JS_SetPropertyStr(ctx, obj, "cert", ssl_bio_dynbuf_jsstring(bio, ctx));
  BIO_free(bio);

  JS_SetPropertyStr(ctx, obj, "subject", js_x509_name(ctx, X509_get_subject_name(cert)));
  JS_SetPropertyStr(ctx, obj, "issuer", js_x509_name(ctx, X509_get_issuer_name(cert)));

  JS_SetPropertyStr(ctx, obj, "serialNumber", js_asn1_integer(ctx, X509_get_serialNumber(cert)));
  JS_SetPropertyStr(ctx, obj, "notBefore", js_asn1_time(ctx, X509_get_notBefore(cert)));
  JS_SetPropertyStr(ctx, obj, "notAfter", js_asn1_time(ctx, X509_get_notAfter(cert)));

#if OPENSSL_VERSION_MAJOR >= 3
  JS_SetPropertyStr(ctx, obj, "selfSigned", JS_NewBool(ctx, X509_self_signed(cert, 0)));
#endif
}

static JSValue
bio_to_jsstring(BIO* b, void* (*get)(BIO*, size_t*), JSContext* ctx) {
  size_t len;
  const char* data;

  if((data = get(b, &len)))
    return JS_NewStringLen(ctx, data, len);

  return JS_UNDEFINED;
}

static JSValue
bio_to_jsarraybuffer(BIO* b, void* (*get)(BIO*, size_t*), JSContext* ctx) {
  size_t len;
  const char* data;

  if((data = get(b, &len)))
    return JS_NewArrayBufferCopy(ctx, data, len);

  return JS_UNDEFINED;
}

static int
bio_dynbuf_write(BIO* b, const char* x, int len) {
  DynBuf* dbuf = BIO_get_app_data(b);

  return dbuf_put(dbuf, x, len) ? -1 : len;
}

static int
bio_dynbuf_write_ex(BIO* b, const char* x, size_t len, size_t* written) {
  DynBuf* dbuf = BIO_get_app_data(b);

  if(dbuf_put(dbuf, x, len))
    return 0;

  if(written)
    *written = len;

  return 1;
}

static int
bio_dynbuf_puts(BIO* b, const char* s) {
  return bio_dynbuf_write(b, s, strlen(s));
}

static int
bio_dynbuf_create(BIO* b) {
  DynBuf* db = malloc(sizeof(DynBuf));

  dbuf_init(db);

  BIO_set_app_data(b, db);
  BIO_set_init(b, 1);
}

static int
bio_dynbuf_destroy(BIO* b) {
  DynBuf* dbuf = BIO_get_app_data(b);

  dbuf_free(dbuf);
  free(dbuf);
}

static BIO_METHOD*
bio_dynbuf_method(void) {
  BIO_METHOD* biom;

  if((biom = BIO_meth_new(BIO_get_new_index(), "DynBuf"))) {
    BIO_meth_set_write(biom, bio_dynbuf_write);
    BIO_meth_set_write_ex(biom, bio_dynbuf_write_ex);
    BIO_meth_set_puts(biom, bio_dynbuf_puts);
    BIO_meth_set_create(biom, bio_dynbuf_create);
    BIO_meth_set_destroy(biom, bio_dynbuf_destroy);
  }

  return biom;
}

BIO*
ssl_bio_dynbuf_new(void) {
  if(!bio_dynbuf)
    bio_dynbuf = bio_dynbuf_method();

  return BIO_new(bio_dynbuf);
}

void
ssl_bio_dynbuf_clear(BIO* b) {
  DynBuf* db = BIO_get_app_data(b);

  free(db->buf);

  db->allocated_size = 0;
  db->size = 0;
  db->buf = NULL;
}

char*
ssl_bio_dynbuf_string(BIO* b) {
  DynBuf* db = BIO_get_app_data(b);

  dbuf_putc(db, '\0');
  --db->size;

  return db->buf;
}

void*
ssl_bio_dynbuf_get(BIO* b, size_t* lenp) {
  DynBuf* db = BIO_get_app_data(b);

  if(lenp)
    *lenp = db->size;

  return db->buf;
}

JSValue
ssl_bio_dynbuf_jsstring(BIO* b, JSContext* ctx) {
  return bio_to_jsstring(b, ssl_bio_dynbuf_get, ctx);
}

JSValue
ssl_bio_dynbuf_jsarraybuffer(BIO* b, JSContext* ctx) {
  return bio_to_jsarraybuffer(b, ssl_bio_dynbuf_get, ctx);
}

static BIO_METHOD* bio_wbuf;

static int
bio_writebuf_write(BIO* b, const char* x, int len) {
  ByteBuffer* wbuf = BIO_get_app_data(b);

  return buffer_append(wbuf, x, len);
}

static int
bio_writebuf_write_ex(BIO* b, const char* x, size_t len, size_t* written) {
  ByteBuffer* wbuf = BIO_get_app_data(b);

  if(buffer_append(wbuf, x, len) < 0)
    return 0;

  if(written)
    *written = len;

  return 1;
}

static int
bio_writebuf_puts(BIO* b, const char* s) {
  return bio_writebuf_write(b, s, strlen(s));
}

static int
bio_writebuf_create(BIO* b) {
  ByteBuffer* wb = malloc(sizeof(ByteBuffer));

  *wb = BUFFER_0();

  BIO_set_app_data(b, wb);
  BIO_set_init(b, 1);
}

static int
bio_writebuf_destroy(BIO* b) {
  ByteBuffer* wbuf = BIO_get_app_data(b);

  buffer_free(wbuf);
  free(wbuf);
}

static BIO_METHOD*
bio_writebuf_method(void) {
  BIO_METHOD* biom;

  if((biom = BIO_meth_new(BIO_get_new_index(), "ByteBuffer"))) {
    BIO_meth_set_write(biom, bio_writebuf_write);
    BIO_meth_set_write_ex(biom, bio_writebuf_write_ex);
    BIO_meth_set_puts(biom, bio_writebuf_puts);
    BIO_meth_set_create(biom, bio_writebuf_create);
    BIO_meth_set_destroy(biom, bio_writebuf_destroy);
  }

  return biom;
}

BIO*
ssl_bio_writebuf_new(void) {
  if(!bio_wbuf)
    bio_wbuf = bio_writebuf_method();

  return BIO_new(bio_wbuf);
}

void
ssl_bio_writebuf_clear(BIO* b) {
  ByteBuffer* wb = BIO_get_app_data(b);

  buffer_free(wb);
}

char*
ssl_bio_writebuf_string(BIO* b) {
  ByteBuffer* wb = BIO_get_app_data(b);

  buffer_putc(wb, '\0');
  --wb->write;

  return buffer_BEGIN(wb);
}

void*
ssl_bio_writebuf_get(BIO* b, size_t* lenp) {
  ByteBuffer* wb = BIO_get_app_data(b);

  if(lenp)
    *lenp = buffer_BYTES(wb);

  return buffer_BEGIN(wb);
}

JSValue
ssl_bio_writebuf_jsstring(BIO* b, JSContext* ctx) {
  return bio_to_jsstring(b, ssl_bio_writebuf_get, ctx);
}

JSValue
ssl_bio_writebuf_jsarraybuffer(BIO* b, JSContext* ctx) {
  return bio_to_jsarraybuffer(b, ssl_bio_writebuf_get, ctx);
}

static BIO_METHOD* bio_rbuf;

static int
bio_readbuf_read(BIO* b, char* x, int len) {
  ByteBuffer* rbuf = BIO_get_app_data(b);

  return buffer_read(rbuf, x, len);
}

static int
bio_readbuf_read_ex(BIO* b, char* x, size_t len, size_t* read) {
  ByteBuffer* rbuf = BIO_get_app_data(b);

  if(buffer_read(rbuf, x, len) < 0)
    return 0;

  if(read)
    *read = len;

  return 1;
}

static int
bio_readbuf_gets(BIO* b, char* x, int len) {
  ByteBuffer* rbuf = BIO_get_app_data(b);

  return buffer_gets(rbuf, x, len);
}

static int
bio_readbuf_destroy(BIO* b) {
  ByteBuffer* rbuf = BIO_get_app_data(b);

  buffer_free(rbuf);
  free(rbuf);
}

static BIO_METHOD*
bio_readbuf_method(void) {
  BIO_METHOD* biom;

  if((biom = BIO_meth_new(BIO_get_new_index(), "rbuf"))) {
    BIO_meth_set_read(biom, bio_readbuf_read);
    BIO_meth_set_read_ex(biom, bio_readbuf_read_ex);
    BIO_meth_set_gets(biom, bio_readbuf_gets);
    BIO_meth_set_destroy(biom, bio_readbuf_destroy);
  }

  return biom;
}

BIO*
ssl_bio_readbuf_new(const void* x, size_t n) {
  ByteBuffer* rb;

  if(!bio_rbuf)
    bio_rbuf = bio_readbuf_method();

  if((rb = malloc(sizeof(ByteBuffer)))) {
    *rb = BUFFER_N(x, n);

    BIO* b = BIO_new(bio_rbuf);
    BIO_set_app_data(b, rb);
    return b;
  }

  return 0;
}
