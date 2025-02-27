#include "minnet-server-proxy.h"
#include <libwebsockets.h>

static MinnetProxyConnection*
proxy_new() {
  MinnetProxyConnection* pc;

  if((pc = malloc(sizeof(MinnetProxyConnection))))
    memset(pc, 0, sizeof(MinnetProxyConnection));

  return pc;
}

static int
proxy_ws_raw_msg_destroy(struct lws_dll2* d, void* user) {
  MinnetProxyMessage* msg = lws_container_of(d, MinnetProxyMessage, list);

  lws_dll2_remove(d);
  free(msg);

  return 0;
}

int
minnet_proxy_server_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  MinnetProxyConnection* pc = (MinnetProxyConnection*)lws_get_opaque_user_data(wsi);

  LOG("PROXY-WS-SERVER", "in=%.*s len=%d", (int)len, (char*)in, (int)len);

  switch(reason) {
    case LWS_CALLBACK_ESTABLISHED: {
      struct lws_client_connect_info info;

      pc = proxy_new();
      lws_set_opaque_user_data(wsi, pc);

      pc->wsi[ACCEPTED] = wsi;

      memset(&info, 0, sizeof(info));

      info.method = "RAW";
      info.context = lws_get_context(wsi);
      info.port = 1234;
      info.address = "127.0.0.1";
      info.ssl_connection = 0;
      info.local_protocol_name = "lws-ws-raw-raw";

      info.opaque_user_data = pc;

      info.pwsi = &pc->wsi[ONWARD];

      if(!lws_client_connect_via_info(&info)) {
        lwsl_warn("%s: onward connection failed\n", __func__);
        return -1;
      }

      break;
    }

    case LWS_CALLBACK_CLOSED: {
      lws_dll2_foreach_safe(&pc->queue[ACCEPTED], NULL, proxy_ws_raw_msg_destroy);
      pc->wsi[ACCEPTED] = NULL;
      lws_set_opaque_user_data(wsi, NULL);

      if(!pc->wsi[ONWARD]) {
        free(pc);
        break;
      }

      if(pc->queue[ONWARD].count) {
        lws_set_timeout(pc->wsi[ONWARD], PENDING_TIMEOUT_KILLED_BY_PROXY_CLIENT_CLOSE, 3);
        break;
      }

      lws_wsi_close(pc->wsi[ONWARD], LWS_TO_KILL_ASYNC);
      break;
    }

    case LWS_CALLBACK_SERVER_WRITEABLE: {
      MinnetProxyMessage* msg;
      uint8_t* data;
      int m, a;

      if(!pc || !pc->queue[ACCEPTED].count)
        break;

      msg = lws_container_of(pc->queue[ACCEPTED].head, MinnetProxyMessage, list);
      data = (uint8_t*)&msg[1] + LWS_PRE;

      m = lws_write(wsi, data, msg->len, LWS_WRITE_TEXT);
      a = (int)msg->len;
      lws_dll2_remove(&msg->list);
      free(msg);

      if(m < a) {
        lwsl_err("ERROR %d writing to ws\n", m);
        return -1;
      }

      if(pc->queue[ACCEPTED].count)
        lws_callback_on_writable(wsi);

      break;
    }

    case LWS_CALLBACK_RECEIVE: {
      MinnetProxyMessage* msg;
      uint8_t* data;

      if(!pc || !pc->wsi[ONWARD])
        break;

      msg = (MinnetProxyMessage*)malloc(sizeof(*msg) + LWS_PRE + len);
      data = (uint8_t*)&msg[1] + LWS_PRE;

      if(!msg) {
        lwsl_user("OOM: dropping\n");
        break;
      }

      memset(msg, 0, sizeof(*msg));
      msg->len = len;
      memcpy(data, in, len);

      lws_dll2_add_tail(&msg->list, &pc->queue[ONWARD]);

      lws_callback_on_writable(pc->wsi[ONWARD]);
      break;
    }

    default: {
      break;
    }
  }

  return 0;
}

int
minnet_proxy_rawclient_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  MinnetProxyConnection* pc = (MinnetProxyConnection*)lws_get_opaque_user_data(wsi);
  MinnetProxyMessage* msg;
  uint8_t* data;
  int m, a;

  LOG("PROXY-RAW-CLIENT", "in=%.*s len=%d", (int)len, (char*)in, (int)len);

  switch(reason) {
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
      lwsl_warn("%s: onward raw connection failed\n", __func__);
      pc->wsi[ONWARD] = NULL;
      break;
    }

    case LWS_CALLBACK_RAW_ADOPT: {
      lwsl_user("LWS_CALLBACK_RAW_ADOPT\n");
      pc->wsi[ONWARD] = wsi;
      lws_callback_on_writable(wsi);
      break;
    }

    case LWS_CALLBACK_RAW_CLOSE: {
      lwsl_user("LWS_CALLBACK_RAW_CLOSE\n");

      lws_dll2_foreach_safe(&pc->queue[ONWARD], NULL, proxy_ws_raw_msg_destroy);

      pc->wsi[ONWARD] = NULL;
      lws_set_opaque_user_data(wsi, NULL);

      if(!pc->wsi[ACCEPTED]) {
        free(pc);
        break;
      }

      if(pc->queue[ACCEPTED].count) {

        lws_set_timeout(pc->wsi[ACCEPTED], PENDING_TIMEOUT_KILLED_BY_PROXY_CLIENT_CLOSE, 3);
        break;
      }

      lws_wsi_close(pc->wsi[ACCEPTED], LWS_TO_KILL_ASYNC);
      break;
    }

    case LWS_CALLBACK_RAW_RX: {
      lwsl_user("LWS_CALLBACK_RAW_RX (%d)\n", (int)len);

      if(!pc || !pc->wsi[ACCEPTED])
        break;

      msg = (MinnetProxyMessage*)malloc(sizeof(*msg) + LWS_PRE + len);
      data = (uint8_t*)&msg[1] + LWS_PRE;

      if(!msg) {
        lwsl_user("OOM: dropping\n");
        break;
      }

      memset(msg, 0, sizeof(*msg));
      msg->len = len;
      memcpy(data, in, len);

      lws_dll2_add_tail(&msg->list, &pc->queue[ACCEPTED]);

      lws_callback_on_writable(pc->wsi[ACCEPTED]);
      break;
    }

    case LWS_CALLBACK_RAW_WRITEABLE: {
      lwsl_user("LWS_CALLBACK_RAW_WRITEABLE\n");

      if(!pc || !pc->queue[ONWARD].count)
        break;

      msg = lws_container_of(pc->queue[ONWARD].head, MinnetProxyMessage, list);
      data = (uint8_t*)&msg[1] + LWS_PRE;

      m = lws_write(wsi, data, msg->len, LWS_WRITE_TEXT);
      a = (int)msg->len;
      lws_dll2_remove(&msg->list);
      free(msg);

      if(m < a) {
        lwsl_err("ERROR %d writing to raw\n", m);
        return -1;
      }

      if(pc->queue[ONWARD].count)
        lws_callback_on_writable(wsi);

      break;
    }
  }

  return 0;
}
