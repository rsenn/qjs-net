#include "minnet-server-proxy.h"
#include <libwebsockets.h>

typedef struct proxy_msg {
  lws_dll2_t list;
  size_t len;

} proxy_msg_t;

typedef struct proxy_conn {
  struct lws* wsi_ws;
  struct lws* wsi_raw;

  lws_dll2_owner_t pending_msg_to_ws;
  lws_dll2_owner_t pending_msg_to_raw;
} proxy_conn_t;



/*int
proxy_server_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  proxy_conn_t* pc = (proxy_conn_t*)lws_get_opaque_user_data(wsi);

  LOG("PROXY-WS-SERVER", "in=%.*s len=%d", (int)len, (char*)in, (int)len);

  switch(reason) {
    case LWS_CALLBACK_ESTABLISHED: {
      struct lws_client_connect_info info;

      pc = proxy_new();
      lws_set_opaque_user_data(wsi, pc);

      pc->wsi_ws = wsi;

      memset(&info, 0, sizeof(info));

      info.method = "RAW";
      info.context = lws_get_context(wsi);
      info.port = 1234;
      info.address = "127.0.0.1";
      info.ssl_connection = 0;
      info.local_protocol_name = "lws-ws-raw-raw";

      info.opaque_user_data = pc;

      info.pwsi = &pc->wsi_raw;

      if(!lws_client_connect_via_info(&info)) {
        lwsl_warn("%s: onward connection failed\n", __func__);
        return -1;
      }

      break;
    }
    case LWS_CALLBACK_CLOSED: {
      lws_dll2_foreach_safe(&pc->pending_msg_to_ws, NULL, proxy_ws_raw_msg_destroy);
      pc->wsi_ws = NULL;
      lws_set_opaque_user_data(wsi, NULL);
      if(!pc->wsi_raw) {
        free(pc);
        break;
      }
      if(pc->pending_msg_to_raw.count) {
        lws_set_timeout(pc->wsi_raw, PENDING_TIMEOUT_KILLED_BY_PROXY_CLIENT_CLOSE, 3);
        break;
      }
      lws_wsi_close(pc->wsi_raw, LWS_TO_KILL_ASYNC);
      break;
    }
    case LWS_CALLBACK_SERVER_WRITEABLE: {
      proxy_msg_t* msg;
      uint8_t* data;
      int m, a;

      if(!pc || !pc->pending_msg_to_ws.count)
        break;

      msg = lws_container_of(pc->pending_msg_to_ws.head, proxy_msg_t, list);
      data = (uint8_t*)&msg[1] + LWS_PRE;

      m = lws_write(wsi, data, msg->len, LWS_WRITE_TEXT);
      a = (int)msg->len;
      lws_dll2_remove(&msg->list);
      free(msg);

      if(m < a) {
        lwsl_err("ERROR %d writing to ws\n", m);
        return -1;
      }

      if(pc->pending_msg_to_ws.count)
        lws_callback_on_writable(wsi);
      break;
    }
    case LWS_CALLBACK_RECEIVE: {
      proxy_msg_t* msg;
      uint8_t* data;

      if(!pc || !pc->wsi_raw)
        break;

      msg = (proxy_msg_t*)malloc(sizeof(*msg) + LWS_PRE + len);
      data = (uint8_t*)&msg[1] + LWS_PRE;

      if(!msg) {
        lwsl_user("OOM: dropping\n");
        break;
      }

      memset(msg, 0, sizeof(*msg));
      msg->len = len;
      memcpy(data, in, len);

      lws_dll2_add_tail(&msg->list, &pc->pending_msg_to_raw);

      lws_callback_on_writable(pc->wsi_raw);
      break;
    }
    default: {
      break;
    }
  }

  return 0;
}
*/
/*int
proxy_rawclient_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  proxy_conn_t* pc = (proxy_conn_t*)lws_get_opaque_user_data(wsi);
  proxy_msg_t* msg;
  uint8_t* data;
  int m, a;
  LOG("PROXY-RAW-CLIENT", "in=%.*s len=%d", (int)len, (char*)in, (int)len);

  switch(reason) {
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      lwsl_warn("%s: onward raw connection failed\n", __func__);
      pc->wsi_raw = NULL;
      break;

    case LWS_CALLBACK_RAW_ADOPT:
      lwsl_user("LWS_CALLBACK_RAW_ADOPT\n");
      pc->wsi_raw = wsi;
      lws_callback_on_writable(wsi);
      break;

    case LWS_CALLBACK_RAW_CLOSE:
      lwsl_user("LWS_CALLBACK_RAW_CLOSE\n");

      lws_dll2_foreach_safe(&pc->pending_msg_to_raw, NULL, proxy_ws_raw_msg_destroy);

      pc->wsi_raw = NULL;
      lws_set_opaque_user_data(wsi, NULL);

      if(!pc->wsi_ws) {

        free(pc);
        break;
      }

      if(pc->pending_msg_to_ws.count) {

        lws_set_timeout(pc->wsi_ws, PENDING_TIMEOUT_KILLED_BY_PROXY_CLIENT_CLOSE, 3);
        break;
      }

      lws_wsi_close(pc->wsi_ws, LWS_TO_KILL_ASYNC);
      break;

    case LWS_CALLBACK_RAW_RX:
      lwsl_user("LWS_CALLBACK_RAW_RX (%d)\n", (int)len);
      if(!pc || !pc->wsi_ws)
        break;

      msg = (proxy_msg_t*)malloc(sizeof(*msg) + LWS_PRE + len);
      data = (uint8_t*)&msg[1] + LWS_PRE;

      if(!msg) {
        lwsl_user("OOM: dropping\n");
        break;
      }

      memset(msg, 0, sizeof(*msg));
      msg->len = len;
      memcpy(data, in, len);

      lws_dll2_add_tail(&msg->list, &pc->pending_msg_to_ws);

      lws_callback_on_writable(pc->wsi_ws);
      break;

    case LWS_CALLBACK_RAW_WRITEABLE:
      lwsl_user("LWS_CALLBACK_RAW_WRITEABLE\n");
      if(!pc || !pc->pending_msg_to_raw.count)
        break;

      msg = lws_container_of(pc->pending_msg_to_raw.head, proxy_msg_t, list);
      data = (uint8_t*)&msg[1] + LWS_PRE;

      m = lws_write(wsi, data, msg->len, LWS_WRITE_TEXT);
      a = (int)msg->len;
      lws_dll2_remove(&msg->list);
      free(msg);

      if(m < a) {
        lwsl_err("ERROR %d writing to raw\n", m);
        return -1;
      }

      if(pc->pending_msg_to_raw.count)
        lws_callback_on_writable(wsi);
      break;
    default: break;
  }

  return 0;
}
*/