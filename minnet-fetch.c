#include "minnet-response.h"
#include "jsutils.h"
#include "buffer.h"
#include <quickjs.h>
#include <curl/curl.h>

struct header_context {
  JSContext* ctx;
  MinnetBuffer* buf;
};

struct curl_callback {
  JSContext* ctx;
  CURLM* multi;
};

struct curl_socket {
  BOOL wantread, wantwrite;
  curl_socket_t sockfd;
};

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
  // JS_FreeValue(ctx, object_proto);
  JS_FreeValue(ctx, keys);
  JS_FreeValue(ctx, names);
  JS_FreeValue(ctx, length);
}

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

      printf("handle_socket sock=%d, wantwrite=%d, wantread=%d\n", sock->sockfd, sock->wantwrite, sock->wantread);

      /*      event_del(sock->event);
            event_assign(sock->event, base, sock->sockfd, events,
              curl_perform, sock);
            event_add(sock->event, NULL);
        */
      break;
    case CURL_POLL_REMOVE:
      if(socketp) {
        /*  event_del(((curl_context_t*) socketp)->event);*/
        js_free(callback_data->ctx, socketp);
        curl_multi_assign(callback_data->multi, s, NULL);
      }
      break;
    default: abort();
  }

  return 0;
}
/*
static void
on_timeout(evutil_socket_t fd, short events, void* arg) {
  int running_handles;
  curl_multi_socket_action(curl_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
  check_multi_info();
}*/

static int
start_timeout(CURLM* multi, long timeout_ms, void* userp) {
  struct curl_callback* callback_data = userp;
  /*if(timeout_ms < 0) {
     evtimer_del(timeout);
   } else {
     if(timeout_ms == 0)
       timeout_ms = 1;
     struct timeval tv;
     tv.tv_sec = timeout_ms / 1000;
     tv.tv_usec = (timeout_ms % 1000) * 1000;
     evtimer_del(timeout);
     evtimer_add(timeout, &tv);
   }*/
  return 0;
}

JSValue
minnet_fetch(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
  CURL* curl = 0;
  CURLM* multi = 0;
  CURLcode curlRes;
  const char* url;
  FILE* fi;
  MinnetResponse* res;
  uint8_t* buffer;
  long bufSize;
  long status;
  char* type;
  const char* body_str = NULL;
  struct curl_slist* headerlist = NULL;
  struct header_context hctx = {ctx, 0};
  struct curl_callback* callback_data = 0;
  int still_running = 1; /* keep number of running handles */

  JSValue resObj = JS_NewObjectClass(ctx, minnet_response_class_id);
  if(JS_IsException(resObj))
    return JS_EXCEPTION;

  res = js_mallocz(ctx, sizeof(*res));

  if(!res) {
    JS_FreeValue(ctx, resObj);
    return JS_EXCEPTION;
  }

  if(!JS_IsString(argv[0]))
    return JS_EXCEPTION;

  url = JS_ToCString(ctx, argv[0]);
  res->url = js_strdup(ctx, url);

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
#if 0
  curlRes = curl_easy_perform(curl);
  if(curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK)
    res->status = status;

  if(curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &type) == CURLE_OK)
    res->type = type ? js_strdup(ctx, type) : 0;

  res->ok = FALSE;

  if(curlRes != CURLE_OK) {
    fprintf(stderr, "CURL failed: %s\n", curl_easy_strerror(curlRes));
    goto finish;
  }

  bufSize = ftell(fi);
  rewind(fi);

  buffer = calloc(1, bufSize + 1);
  if(!buffer) {
    fclose(fi);
    fputs("memory alloc fails", stderr);
    goto finish;
  }

  /* copy the file into the buffer */
  if(1 != fread(buffer, bufSize, 1, fi)) {
    fclose(fi);
    free(buffer);
    fputs("entire read fails", stderr);
    goto finish;
  }

  fclose(fi);
  res->ok = TRUE;
  res->body = BUFFER_N(buffer, bufSize);
#endif

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