#define _GNU_SOURCE
#include "minnet-response.h"
#define _GNU_SOURCE
#include "jsutils.h"
#define _GNU_SOURCE
#include "buffer.h"
#define _GNU_SOURCE
#include <quickjs.h>
#define _GNU_SOURCE
#include <curl/curl.h>
#define _GNU_SOURCE

#define _GNU_SOURCE
struct header_context {
#define _GNU_SOURCE
  JSContext* ctx;
#define _GNU_SOURCE
  MinnetBuffer* buf;
#define _GNU_SOURCE
};
#define _GNU_SOURCE

#define _GNU_SOURCE
struct curl_callback {
#define _GNU_SOURCE
  JSContext* ctx;
#define _GNU_SOURCE
  CURLM* multi;
#define _GNU_SOURCE
};
#define _GNU_SOURCE

#define _GNU_SOURCE
struct curl_socket {
#define _GNU_SOURCE
  BOOL wantread, wantwrite;
#define _GNU_SOURCE
  curl_socket_t sockfd;
#define _GNU_SOURCE
};
#define _GNU_SOURCE

#define _GNU_SOURCE
static void
#define _GNU_SOURCE
header_list(JSContext* ctx, JSValueConst headers, struct curl_slist** list_ptr) {
#define _GNU_SOURCE
  JSValue global_obj, object_ctor, keys, names, length;
#define _GNU_SOURCE
  int i;
#define _GNU_SOURCE
  int32_t len;
#define _GNU_SOURCE

#define _GNU_SOURCE
  global_obj = JS_GetGlobalObject(ctx);
#define _GNU_SOURCE
  object_ctor = JS_GetPropertyStr(ctx, global_obj, "Object");
#define _GNU_SOURCE
  keys = JS_GetPropertyStr(ctx, object_ctor, "keys");
#define _GNU_SOURCE

#define _GNU_SOURCE
  names = JS_Call(ctx, keys, object_ctor, 1, (JSValueConst*)&headers);
#define _GNU_SOURCE
  length = JS_GetPropertyStr(ctx, names, "length");
#define _GNU_SOURCE

#define _GNU_SOURCE
  JS_ToInt32(ctx, &len, length);
#define _GNU_SOURCE

#define _GNU_SOURCE
  for(i = 0; i < len; i++) {
#define _GNU_SOURCE
    JSValue key, value;
#define _GNU_SOURCE
    const char *key_str, *value_str;
#define _GNU_SOURCE
    size_t key_len, value_len;
#define _GNU_SOURCE
    key = JS_GetPropertyUint32(ctx, names, i);
#define _GNU_SOURCE
    key_str = JS_ToCString(ctx, key);
#define _GNU_SOURCE
    key_len = strlen(key_str);
#define _GNU_SOURCE

#define _GNU_SOURCE
    value = JS_GetPropertyStr(ctx, headers, key_str);
#define _GNU_SOURCE
    value_str = JS_ToCString(ctx, value);
#define _GNU_SOURCE
    value_len = strlen(value_str);
#define _GNU_SOURCE

#define _GNU_SOURCE
    {
#define _GNU_SOURCE
      size_t bufsize = 0;
#define _GNU_SOURCE
      char buf[key_len + 2 + value_len + 2 + 1];
#define _GNU_SOURCE

#define _GNU_SOURCE
      strcpy(&buf[bufsize], key_str);
#define _GNU_SOURCE
      bufsize += key_len;
#define _GNU_SOURCE
      strcpy(&buf[bufsize], ": ");
#define _GNU_SOURCE
      bufsize += 2;
#define _GNU_SOURCE
      strcpy(&buf[bufsize], value_str);
#define _GNU_SOURCE
      bufsize += value_len;
#define _GNU_SOURCE
      strcpy(&buf[bufsize], "\0\n");
#define _GNU_SOURCE
      bufsize += 2;
#define _GNU_SOURCE

#define _GNU_SOURCE
      JS_FreeCString(ctx, key_str);
#define _GNU_SOURCE
      JS_FreeCString(ctx, value_str);
#define _GNU_SOURCE

#define _GNU_SOURCE
      *list_ptr = curl_slist_append(*list_ptr, buf);
#define _GNU_SOURCE
    }
#define _GNU_SOURCE
  }
#define _GNU_SOURCE

#define _GNU_SOURCE
  JS_FreeValue(ctx, global_obj);
#define _GNU_SOURCE
  JS_FreeValue(ctx, object_ctor);
#define _GNU_SOURCE
  // JS_FreeValue(ctx, object_proto);
#define _GNU_SOURCE
  JS_FreeValue(ctx, keys);
#define _GNU_SOURCE
  JS_FreeValue(ctx, names);
#define _GNU_SOURCE
  JS_FreeValue(ctx, length);
#define _GNU_SOURCE
}
#define _GNU_SOURCE

#define _GNU_SOURCE
static size_t
#define _GNU_SOURCE
header_callback(char* x, size_t n, size_t nitems, void* userdata) {
#define _GNU_SOURCE
  struct header_context* hc = userdata;
#define _GNU_SOURCE
  size_t i;
#define _GNU_SOURCE

#define _GNU_SOURCE
  for(i = nitems; i > 0; --i)
#define _GNU_SOURCE
    if(!(x[i - 1] == '\r' || x[i - 1] == '\n'))
#define _GNU_SOURCE
      break;
#define _GNU_SOURCE

#define _GNU_SOURCE
  if(i > 0 && buffer_append(hc->buf, x, i + 1, hc->ctx) > 0)
#define _GNU_SOURCE
    hc->buf->write[-1] = '\n';
#define _GNU_SOURCE

#define _GNU_SOURCE
  return nitems * n;
#define _GNU_SOURCE
}
#define _GNU_SOURCE

#define _GNU_SOURCE
static int
#define _GNU_SOURCE
handle_socket(CURL* easy, curl_socket_t s, int action, void* userp, void* socketp) {
#define _GNU_SOURCE
  struct curl_callback* callback_data = userp;
#define _GNU_SOURCE
  struct curl_socket* sock;
#define _GNU_SOURCE
  int events = 0;
#define _GNU_SOURCE

#define _GNU_SOURCE
  switch(action) {
#define _GNU_SOURCE
    case CURL_POLL_IN:
#define _GNU_SOURCE
    case CURL_POLL_OUT:
#define _GNU_SOURCE
    case CURL_POLL_INOUT:
#define _GNU_SOURCE
      sock = (struct curl_socket*)(socketp ? socketp : js_mallocz(callback_data->ctx, sizeof(struct curl_socket)));
#define _GNU_SOURCE

#define _GNU_SOURCE
      curl_multi_assign(callback_data->multi, s, (void*)sock);
#define _GNU_SOURCE

#define _GNU_SOURCE
      sock->sockfd = s;
#define _GNU_SOURCE

#define _GNU_SOURCE
      if(action != CURL_POLL_IN)
#define _GNU_SOURCE
        sock->wantwrite = TRUE;
#define _GNU_SOURCE
      if(action != CURL_POLL_OUT)
#define _GNU_SOURCE
        sock->wantread = TRUE;
#define _GNU_SOURCE

#define _GNU_SOURCE
      printf("handle_socket sock=%d, wantwrite=%d, wantread=%d\n", sock->sockfd, sock->wantwrite, sock->wantread);
#define _GNU_SOURCE

#define _GNU_SOURCE
      /*      event_del(sock->event);
#define _GNU_SOURCE
            event_assign(sock->event, base, sock->sockfd, events,
#define _GNU_SOURCE
              curl_perform, sock);
#define _GNU_SOURCE
            event_add(sock->event, NULL);
#define _GNU_SOURCE
        */
#define _GNU_SOURCE
      break;
#define _GNU_SOURCE
    case CURL_POLL_REMOVE:
#define _GNU_SOURCE
      if(socketp) {
#define _GNU_SOURCE
        /*  event_del(((curl_context_t*) socketp)->event);*/
#define _GNU_SOURCE
        js_free(callback_data->ctx, socketp);
#define _GNU_SOURCE
        curl_multi_assign(callback_data->multi, s, NULL);
#define _GNU_SOURCE
      }
#define _GNU_SOURCE
      break;
#define _GNU_SOURCE
    default: abort();
#define _GNU_SOURCE
  }
#define _GNU_SOURCE

#define _GNU_SOURCE
  return 0;
#define _GNU_SOURCE
}
#define _GNU_SOURCE

#define _GNU_SOURCE
/*
#define _GNU_SOURCE
static void
#define _GNU_SOURCE
on_timeout(evutil_socket_t fd, short events, void* arg) {
#define _GNU_SOURCE
  int running_handles;
#define _GNU_SOURCE
  curl_multi_socket_action(curl_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
#define _GNU_SOURCE
  check_multi_info();
#define _GNU_SOURCE
}*/
#define _GNU_SOURCE

#define _GNU_SOURCE
static int
#define _GNU_SOURCE
start_timeout(CURLM* multi, long timeout_ms, void* userp) {
#define _GNU_SOURCE
  struct curl_callback* callback_data = userp;
#define _GNU_SOURCE
  /*if(timeout_ms < 0) {
#define _GNU_SOURCE
     evtimer_del(timeout);
#define _GNU_SOURCE
   } else {
#define _GNU_SOURCE
     if(timeout_ms == 0)
#define _GNU_SOURCE
       timeout_ms = 1;
#define _GNU_SOURCE
     struct timeval tv;
#define _GNU_SOURCE
     tv.tv_sec = timeout_ms / 1000;
#define _GNU_SOURCE
     tv.tv_usec = (timeout_ms % 1000) * 1000;
#define _GNU_SOURCE
     evtimer_del(timeout);
#define _GNU_SOURCE
     evtimer_add(timeout, &tv);
#define _GNU_SOURCE
   }*/
#define _GNU_SOURCE
  return 0;
#define _GNU_SOURCE
}
#define _GNU_SOURCE

#define _GNU_SOURCE
JSValue
#define _GNU_SOURCE
minnet_fetch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
#define _GNU_SOURCE
  CURL* curl = 0;
#define _GNU_SOURCE
  CURLM* multi = 0;
#define _GNU_SOURCE
  CURLcode curlRes;
#define _GNU_SOURCE
  const char* url;
#define _GNU_SOURCE
  FILE* fi;
#define _GNU_SOURCE
  MinnetResponse* res;
#define _GNU_SOURCE
  uint8_t* buffer;
#define _GNU_SOURCE
  long bufSize;
#define _GNU_SOURCE
  long status;
#define _GNU_SOURCE
  char* type;
#define _GNU_SOURCE
  const char* body_str = NULL;
#define _GNU_SOURCE
  struct curl_slist* headerlist = NULL;
#define _GNU_SOURCE
  struct header_context hctx = {ctx, 0};
#define _GNU_SOURCE
  struct curl_callback* callback_data = 0;
#define _GNU_SOURCE
  int still_running = 1; /* keep number of running handles */
#define _GNU_SOURCE

#define _GNU_SOURCE
  JSValue resObj = JS_NewObjectClass(ctx, minnet_response_class_id);
#define _GNU_SOURCE
  if(JS_IsException(resObj))
#define _GNU_SOURCE
    return JS_EXCEPTION;
#define _GNU_SOURCE

#define _GNU_SOURCE
  res = js_mallocz(ctx, sizeof(*res));
#define _GNU_SOURCE

#define _GNU_SOURCE
  if(!res) {
#define _GNU_SOURCE
    JS_FreeValue(ctx, resObj);
#define _GNU_SOURCE
    return JS_EXCEPTION;
#define _GNU_SOURCE
  }
#define _GNU_SOURCE

#define _GNU_SOURCE
  if(!JS_IsString(argv[0]))
#define _GNU_SOURCE
    return JS_EXCEPTION;
#define _GNU_SOURCE

#define _GNU_SOURCE
  url = JS_ToCString(ctx, argv[0]);
#define _GNU_SOURCE
  res->url = js_strdup(ctx, url);
#define _GNU_SOURCE

#define _GNU_SOURCE
  if(argc > 1 && JS_IsObject(argv[1])) {
#define _GNU_SOURCE
    JSValue method, body, headers;
#define _GNU_SOURCE
    const char* method_str;
#define _GNU_SOURCE
    method = JS_GetPropertyStr(ctx, argv[1], "method");
#define _GNU_SOURCE
    body = JS_GetPropertyStr(ctx, argv[1], "body");
#define _GNU_SOURCE
    headers = JS_GetPropertyStr(ctx, argv[1], "headers");
#define _GNU_SOURCE

#define _GNU_SOURCE
    if(!JS_IsUndefined(headers))
#define _GNU_SOURCE
      header_list(ctx, headers, &headerlist);
#define _GNU_SOURCE

#define _GNU_SOURCE
    method_str = JS_ToCString(ctx, method);
#define _GNU_SOURCE

#define _GNU_SOURCE
    if(!JS_IsUndefined(body) || !strcasecmp(method_str, "post")) {
#define _GNU_SOURCE
      body_str = JS_ToCString(ctx, body);
#define _GNU_SOURCE
    }
#define _GNU_SOURCE

#define _GNU_SOURCE
    JS_FreeCString(ctx, method_str);
#define _GNU_SOURCE

#define _GNU_SOURCE
    JS_FreeValue(ctx, method);
#define _GNU_SOURCE
    JS_FreeValue(ctx, body);
#define _GNU_SOURCE
    JS_FreeValue(ctx, headers);
#define _GNU_SOURCE
  }
#define _GNU_SOURCE

#define _GNU_SOURCE
  curl_global_init(CURL_GLOBAL_DEFAULT);
#define _GNU_SOURCE

#define _GNU_SOURCE
  if(!(multi = curl_multi_init()) || !(curl = curl_easy_init()) || !(callback_data = js_mallocz(ctx, sizeof(struct curl_callback)))) {
#define _GNU_SOURCE
    resObj = JS_ThrowOutOfMemory(ctx);
#define _GNU_SOURCE
    goto fail;
#define _GNU_SOURCE
  }
#define _GNU_SOURCE

#define _GNU_SOURCE
  callback_data->ctx = ctx;
#define _GNU_SOURCE
  callback_data->multi = multi;
#define _GNU_SOURCE

#define _GNU_SOURCE
  curl_multi_setopt(multi, CURLMOPT_SOCKETFUNCTION, handle_socket);
#define _GNU_SOURCE
  curl_multi_setopt(multi, CURLMOPT_SOCKETDATA, callback_data);
#define _GNU_SOURCE
  curl_multi_setopt(multi, CURLMOPT_TIMERFUNCTION, start_timeout);
#define _GNU_SOURCE
  curl_multi_setopt(multi, CURLMOPT_TIMERDATA, callback_data);
#define _GNU_SOURCE

#define _GNU_SOURCE
  fi = tmpfile();
#define _GNU_SOURCE
  hctx.buf = &res->headers;
#define _GNU_SOURCE

#define _GNU_SOURCE
  curl_easy_setopt(curl, CURLOPT_URL, url);
#define _GNU_SOURCE
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
#define _GNU_SOURCE
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "minimal-network-quickjs");
#define _GNU_SOURCE
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);
#define _GNU_SOURCE
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fi);
#define _GNU_SOURCE
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, &header_callback);
#define _GNU_SOURCE
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hctx);
#define _GNU_SOURCE

#define _GNU_SOURCE
  if(body_str)
#define _GNU_SOURCE
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str);
#define _GNU_SOURCE

#define _GNU_SOURCE
  curl_multi_add_handle(multi, curl);
#define _GNU_SOURCE

#define _GNU_SOURCE
  if(still_running) {
#define _GNU_SOURCE
    CURLMsg* msg;
#define _GNU_SOURCE
    int queued;
#define _GNU_SOURCE
    CURLMcode mc = curl_multi_perform(multi, &still_running);
#define _GNU_SOURCE
  }
#define _GNU_SOURCE
#if 0
#define _GNU_SOURCE
  curlRes = curl_easy_perform(curl);
#define _GNU_SOURCE
  if(curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK)
#define _GNU_SOURCE
    res->status = status;
#define _GNU_SOURCE

#define _GNU_SOURCE
  if(curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &type) == CURLE_OK)
#define _GNU_SOURCE
    res->type = type ? js_strdup(ctx, type) : 0;
#define _GNU_SOURCE

#define _GNU_SOURCE
  res->ok = FALSE;
#define _GNU_SOURCE

#define _GNU_SOURCE
  if(curlRes != CURLE_OK) {
#define _GNU_SOURCE
    fprintf(stderr, "CURL failed: %s\n", curl_easy_strerror(curlRes));
#define _GNU_SOURCE
    goto finish;
#define _GNU_SOURCE
  }
#define _GNU_SOURCE

#define _GNU_SOURCE
  bufSize = ftell(fi);
#define _GNU_SOURCE
  rewind(fi);
#define _GNU_SOURCE

#define _GNU_SOURCE
  buffer = calloc(1, bufSize + 1);
#define _GNU_SOURCE
  if(!buffer) {
#define _GNU_SOURCE
    fclose(fi);
#define _GNU_SOURCE
    fputs("memory alloc fails", stderr);
#define _GNU_SOURCE
    goto finish;
#define _GNU_SOURCE
  }
#define _GNU_SOURCE

#define _GNU_SOURCE
  /* copy the file into the buffer */
#define _GNU_SOURCE
  if(1 != fread(buffer, bufSize, 1, fi)) {
#define _GNU_SOURCE
    fclose(fi);
#define _GNU_SOURCE
    free(buffer);
#define _GNU_SOURCE
    fputs("entire read fails", stderr);
#define _GNU_SOURCE
    goto finish;
#define _GNU_SOURCE
  }
#define _GNU_SOURCE

#define _GNU_SOURCE
  fclose(fi);
#define _GNU_SOURCE
  res->ok = TRUE;
#define _GNU_SOURCE
  res->body = BUFFER_N(buffer, bufSize);
#define _GNU_SOURCE
#endif
#define _GNU_SOURCE

#define _GNU_SOURCE
finish:
#define _GNU_SOURCE

#define _GNU_SOURCE
  curl_slist_free_all(headerlist);
#define _GNU_SOURCE
  if(body_str)
#define _GNU_SOURCE
    JS_FreeCString(ctx, body_str);
#define _GNU_SOURCE

#define _GNU_SOURCE
  JS_SetOpaque(resObj, res);
#define _GNU_SOURCE

#define _GNU_SOURCE
fail:
#define _GNU_SOURCE
  if(callback_data)
#define _GNU_SOURCE
    js_free(ctx, callback_data);
#define _GNU_SOURCE
  if(curl)
#define _GNU_SOURCE
    curl_easy_cleanup(curl);
#define _GNU_SOURCE
  if(multi)
#define _GNU_SOURCE
    curl_multi_cleanup(multi);
#define _GNU_SOURCE

#define _GNU_SOURCE
  curl_global_cleanup();
#define _GNU_SOURCE
  return resObj;
#define _GNU_SOURCE
}
