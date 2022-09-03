#define _GNU_SOURCE
#include "minnet-request.h"
#include "minnet-response.h"
#include "minnet-client.h"
#include "buffer.h"
#include "closure.h"
#include "jsutils.h"
#include <strings.h>
#include <quickjs.h>

#ifndef USE_CURL

/*typedef struct fetch_closure {
  int ref_count;
  MinnetClient* client;
} MinnetFetch;

MinnetFetch*
fetch_new(JSContext* ctx) {
  MinnetFetch* ret;

  if((ret = js_mallocz(ctx, sizeof(MinnetFetch))))
    ++ret->ref_count;

  return ret;
}

MinnetFetch*
fetch_dup(MinnetFetch* c) {
  ++c->ref_count;
  return c;
}

void
closure_free(void* ptr) {
  MinnetFetch* closure = ptr;

  if(--closure->ref_count == 0) {
    if(closure->client) {
      JSContext* ctx = closure->client->context.js;

      client_free(closure->client);

      js_free(ctx, closure);
    }
  }
}*/

enum {
  ON_HTTP = 0,
  ON_ERROR,
  ON_CLOSE,
  ON_FD,
};

static JSValue
fetch_handler(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic, void* opaque) {
  MinnetClosure* closure = opaque;
  MinnetClient* client = closure->pointer;

  // printf("%s magic=%s client=%p\n", __func__, magic == ON_HTTP ? "ON_HTTP" : magic == ON_ERROR ? "ON_ERROR" : "ON_FD", client);

  switch(magic) {
    case ON_HTTP: {
      if(js_promise_pending(&client->promise))
        js_promise_resolve(ctx, &client->promise, argv[1]);
      break;
    }

    case ON_CLOSE:
    case ON_ERROR: {
      const char* str = JS_ToCString(ctx, argv[1]);
      JSValue err = js_error_new(ctx, "%s: %s", magic == ON_CLOSE ? "onClose" : "onError", str);
      JS_FreeCString(ctx, str);

      //  JS_SetPropertyStr(ctx, err, "message", JS_DupValue(ctx, argv[1]));
      if(js_promise_pending(&client->promise))
        js_promise_reject(ctx, &client->promise, err);
      JS_FreeValue(ctx, err);
      break;
    }

    case ON_FD: {
      JSValue os, tmp, set_write, set_read, args[2] = {argv[0], JS_NULL};
      os = js_global_get(ctx, "os");
      if(!JS_IsObject(os))
        return JS_ThrowTypeError(ctx, "globalThis.os must be imported module");
      set_read = JS_GetPropertyStr(ctx, os, "setReadHandler");
      set_write = JS_GetPropertyStr(ctx, os, "setWriteHandler");
      args[1] = argv[1];
      tmp = JS_Call(ctx, set_read, os, 2, args);
      JS_FreeValue(ctx, tmp);
      args[1] = argv[2];
      tmp = JS_Call(ctx, set_write, os, 2, args);
      JS_FreeValue(ctx, tmp);
      JS_FreeValue(ctx, os);
      JS_FreeValue(ctx, set_write);
      JS_FreeValue(ctx, set_read);
      break;
    }
  }

  return JS_UNDEFINED;
}

JSValue
minnet_fetch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  JSValue ret, handlers[4], args[2];
  MinnetClosure* cc;
  // MinnetFetch* fc;

  if(argc >= 2 && !JS_IsObject(argv[1]))
    return JS_ThrowTypeError(ctx, "argument 2 must be an object");
  /*
    if(!(fc = fetch_new(ctx)))
      return JS_ThrowOutOfMemory(ctx);*/

  if(!(cc = closure_new(ctx)))
    return JS_ThrowOutOfMemory(ctx);

  args[0] = argv[0];
  args[1] = argc <= 1 ? JS_NewObject(ctx) : JS_DupValue(ctx, argv[1]);

  handlers[0] = JS_NewCClosure(ctx, &fetch_handler, 2, ON_HTTP, closure_dup(cc), closure_free);
  handlers[1] = JS_NewCClosure(ctx, &fetch_handler, 2, ON_ERROR, closure_dup(cc), closure_free);
  handlers[2] = JS_NewCClosure(ctx, &fetch_handler, 2, ON_CLOSE, closure_dup(cc), closure_free);
  handlers[3] = JS_NewCClosure(ctx, &fetch_handler, 3, ON_FD, closure_dup(cc), closure_free);

  JS_SetPropertyStr(ctx, args[1], "onHttp", handlers[0]);
  JS_SetPropertyStr(ctx, args[1], "onError", handlers[1]);
  JS_SetPropertyStr(ctx, args[1], "onClose", handlers[2]);
  JS_SetPropertyStr(ctx, args[1], "onFd", handlers[3]);
  JS_SetPropertyStr(ctx, args[1], "block", JS_FALSE);

  ret = minnet_client_closure(ctx, this_val, 2, args, 0, cc);

  JS_FreeValue(ctx, args[1]);

  // printf("%s url=%s client=%p\n", __func__, JS_ToCString(ctx, args[0]), cc->client);

  cc->pointer = client_dup(cc->pointer);

  return ret;
}

#else
#include <curl/curl.h>

struct header_context {
  JSContext* ctx;
  ByteBuffer* buf;
};

struct curl_callback {
  JSContext* ctx;
  CURLM* multi;
};

struct curl_socket {
  BOOL wantread, wantwrite;
  curl_socket_t sockfd;
};

static size_t
header_callback(char* x, size_t n, size_t nitems, void* userdata) {
  struct header_context* hc = userdata;
  size_t i;
  for(i = nitems; i > 0; --i)
    if(!(x[i - 1] == '\r' || x[i - 1] == '\n'))
      break;
  if(i > 0 && buffer_append(hc->buf, x, i + 1, hc->ctx) > 0)
    hc->buf->write[-1] = '\n';
  return nitems * n;
}

static int
start_timeout(CURLM* multi, long timeout_ms, void* userp) {
  struct curl_callback* callback_data = userp;
  return 0;
}

static void
header_list(JSContext* ctx, JSValueConst headers, struct curl_slist** list_ptr) {
  JSValue global_obj, object_ctor, keys, names, length;
  int i;
  int32_t len;
  global_obj = JS_GetGlobalObject(ctx);
  object_ctor = JS_GetPropertyStr(ctx, global_obj, "Object");
  keys = JS_GetPropertyStr(ctx, object_ctor, "keys");
  names = JS_Call(ctx, keys, object_ctor, 1, (JSValueConst*)&headers);
  length = JS_GetPropertyStr(ctx, names, "length");
  JS_ToInt32(ctx, &len, length);

  for(i = 0; i < len; i++) {
    JSValue key, value;
    const char *key_str, *value_str;
    size_t key_len, value_len;
    key = JS_GetPropertyUint32(ctx, names, i);
    key_str = JS_ToCString(ctx, key);
    key_len = strlen(key_str);
    value = JS_GetPropertyStr(ctx, headers, key_str);
    value_str = JS_ToCString(ctx, value);
    value_len = strlen(value_str);
    {
      size_t bufsize = 0;
      char buf[key_len + 2 + value_len + 2 + 1];
      strcpy(&buf[bufsize], key_str);
      bufsize += key_len;
      strcpy(&buf[bufsize], ": ");
      bufsize += 2;
      strcpy(&buf[bufsize], value_str);
      bufsize += value_len;
      strcpy(&buf[bufsize], "\0\n");
      bufsize += 2;
      JS_FreeCString(ctx, key_str);
      JS_FreeCString(ctx, value_str);
      *list_ptr = curl_slist_append(*list_ptr, buf);
    }
  }

  JS_FreeValue(ctx, global_obj);
  JS_FreeValue(ctx, object_ctor);
  JS_FreeValue(ctx, keys);
  JS_FreeValue(ctx, names);
  JS_FreeValue(ctx, length);
}

static int
handle_socket(CURL* easy, curl_socket_t s, int action, void* userp, void* socketp) {
  struct curl_callback* callback_data = userp;
  struct curl_socket* sock;
  int events = 0;
  switch(action) {
    case CURL_POLL_IN:
    case CURL_POLL_OUT:
    case CURL_POLL_INOUT:
      sock = (struct curl_socket*)(socketp ? socketp : js_mallocz(callback_data->ctx, sizeof(struct curl_socket)));
      curl_multi_assign(callback_data->multi, s, (void*)sock);

      sock->sockfd = s;
      if(action != CURL_POLL_IN)
        sock->wantwrite = TRUE;
      if(action != CURL_POLL_OUT)
        sock->wantread = TRUE;
      // fprintf(stderr, "handle_socket sock=%d, wantwrite=%d, wantread=%d\n", sock->sockfd, sock->wantwrite, sock->wantread);

      break;
    case CURL_POLL_REMOVE:
      if(socketp) {
        js_free(callback_data->ctx, socketp);
        curl_multi_assign(callback_data->multi, s, NULL);
      }

      break;
    default: abort();
  }

  return 0;
}

JSValue
minnet_fetch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[]) {
  CURL* curl = 0;
  CURLM* multi = 0;
  CURLcode curlRes;
  // const char* url;
  FILE* fi;
  MinnetRequest* req;
  MinnetResponse* res;
  uint8_t* buffer;
  long bufSize;
  long status;
  char* type;
  const char* body_str = NULL;
  struct curl_slist* headerlist = NULL;
  struct header_context hctx = {ctx, 0};
  struct curl_callback* callback_data = 0;
  int still_running = 1;

  req = request_from(argc, argv, ctx);

  JSValue resObj = JS_NewObjectClass(ctx, minnet_response_class_id);
  if(JS_IsException(resObj))
    return JS_EXCEPTION;
  res = js_mallocz(ctx, sizeof(*res));

  if(!res) {
    JS_FreeValue(ctx, resObj);
    return JS_EXCEPTION;
  }

  /*if(!JS_IsString(argv[0]))
    return JS_EXCEPTION;
  url = JS_ToCString(ctx, argv[0]);*/

  url_copy(&res->url, &req->url, ctx);

  if(argc > 1 && JS_IsObject(argv[1])) {
    JSValue method, body, headers;
    const char* method_str;
    method = JS_GetPropertyStr(ctx, argv[1], "method");
    body = JS_GetPropertyStr(ctx, argv[1], "body");
    headers = JS_GetPropertyStr(ctx, argv[1], "headers");
    if(!JS_IsUndefined(headers))
      header_list(ctx, headers, &headerlist);
    method_str = JS_ToCString(ctx, method);

    if(!JS_IsUndefined(body) || !strcasecmp(method_str, "post")) {
      body_str = JS_ToCString(ctx, body);
    }

    JS_FreeCString(ctx, method_str);

    JS_FreeValue(ctx, method);
    JS_FreeValue(ctx, body);
    JS_FreeValue(ctx, headers);
  }

  curl_global_init(CURL_GLOBAL_DEFAULT);

  if(!(multi = curl_multi_init()) || !(curl = curl_easy_init()) || !(callback_data = js_mallocz(ctx, sizeof(struct curl_callback)))) {
    resObj = JS_ThrowOutOfMemory(ctx);
    goto fail;
  }

  callback_data->ctx = ctx;
  callback_data->multi = multi;
  curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, handle_socket);
  curl_multi_setopt(multi, CURLMOPT_SOCKETDATA, callback_data);
  curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, start_timeout);
  curl_multi_setopt(multi, CURLMOPT_TIMERDATA, callback_data);
  fi = tmpfile();
  hctx.buf = &res->headers;
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "minimal-network-quickjs");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fi);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &header_callback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hctx);
  if(body_str)
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str);
  curl_multi_add_handle(multi, curl);

  if(still_running) {
    CURLMsg* msg;
    int queued;
    CURLMcode mc = curl_multi_perform(multi, &still_running);
  }

finish:

  curl_slist_free_all(headerlist);
  if(body_str)
    JS_FreeCString(ctx, body_str);
  JS_SetOpaque(resObj, res);

fail:
  if(callback_data)
    js_free(ctx, callback_data);
  if(curl)
    curl_easy_cleanup(curl);
  if(multi)
    curl_multi_cleanup(multi);
  curl_global_cleanup();
  return resObj;
}
#endif
