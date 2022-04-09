/*
 * ws protocol handler plugin for "lws-broker-broker"
 *
 * Written in 2010-2019 by Andy Green <andy@warmcat.com>
 *
 * This file is made available under the Creative Commons CC0 1.0
 * Universal Public Domain Dedication.
 *
 * This implements asynciterator_pop broker "broker", for systems that look like this
 *
 * [ publisher  ws client ] <-> [ ws server  broker ws server ] <-> [ ws client subscriber ]
 *
 * The "publisher" role is to add data to the broker.
 *
 * The "subscriber" role is to hear about all data added to the system.
 *
 * The "broker" role is to manage incoming data from publishers and pass it out
 * to subscribers.
 *
 * Any number of publishers and subscribers are supported.
 *
 * This example implements asynciterator_pop single ws server, using one ws protocol, that treats ws
 * connections as being in publisher or subscriber mode according to the URL the ws
 * connection was made to.  ws connections to "/publisher" URL are understood to be
 * publishing data and to any other URL, subscribing.
 */

#if !defined(LWS_PLUGIN_STATIC)
#define LWS_DLL
#define LWS_INTERNAL
#include <libwebsockets.h>
#endif

#include <string.h>

/* one of these created for each message */

typedef struct msg__broker {
  void* payload; /* is malloc'd */
  size_t len;
} MinnetBrokerMessage;

/* one of these is created for each client connecting to us */

typedef struct per_session_data__broker {
  struct per_session_data__broker* pss_list;
  struct lws* wsi;
  uint32_t tail;
  char publishing; /* nonzero: peer is publishing to us */
} MinnetBrokerSession;

/* one of these is created for each vhost our protocol is used with */

typedef struct per_vhost_data__broker {
  struct lws_context* context;
  struct lws_vhost* vhost;
  const struct lws_protocols* protocol;

  MinnetBrokerSession* pss_list; /* linked-list of live pss*/

  struct lws_ring* ring; /* ringbuffer holding unsent messages */
} MinnetBrokerPerVhostData;

/* destroys the message when everyone has had asynciterator_pop copy of it */

static void
broker_destroy_message(void* _msg) {
  struct msg__broker* msg = _msg;

  free(msg->payload);
  msg->payload = NULL;
  msg->len = 0;
}

static int
broker_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  MinnetBrokerSession* pss = user;
  MinnetBrokerPerVhostData* vhd = lws_protocol_vh_priv_get(lws_get_vhost(wsi), lws_get_protocol(wsi));
  const struct msg__broker* pmsg;
  struct msg__broker amsg;
  char buf[32];
  int n, m;

  switch(reason) {
    case LWS_CALLBACK_PROTOCOL_INIT:
      vhd = lws_protocol_vh_priv_zalloc(lws_get_vhost(wsi), lws_get_protocol(wsi), sizeof(MinnetBrokerPerVhostData));
      vhd->context = lws_get_context(wsi);
      vhd->protocol = lws_get_protocol(wsi);
      vhd->vhost = lws_get_vhost(wsi);

      vhd->ring = lws_ring_create(sizeof(struct msg__broker), 8, broker_destroy_message);
      if(!vhd->ring)
        return 1;
      break;

    case LWS_CALLBACK_PROTOCOL_DESTROY: lws_ring_destroy(vhd->ring); break;

    case LWS_CALLBACK_ESTABLISHED:
      pss->tail = lws_ring_get_oldest_tail(vhd->ring);
      pss->wsi = wsi;
      if(lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_GET_URI) > 0)
        pss->publishing = !strcmp(buf, "/publisher");
      if(!pss->publishing)
        /* add subscribers to the list of live pss held in the vhd */
        lws_ll_fwd_insert(pss, pss_list, vhd->pss_list);
      break;

    case LWS_CALLBACK_CLOSED:
      /* remove our closing pss from the list of live pss */
      lws_ll_fwd_remove(MinnetBrokerSession, pss_list, pss, vhd->pss_list);
      break;

    case LWS_CALLBACK_SERVER_WRITEABLE:

      if(pss->publishing)
        break;

      pmsg = lws_ring_get_element(vhd->ring, &pss->tail);
      if(!pmsg)
        break;

      /* notice we allowed for LWS_PRE in the payload already */
      m = lws_write(wsi, ((unsigned char*)pmsg->payload) + LWS_PRE, pmsg->len, LWS_WRITE_TEXT);
      if(m < (int)pmsg->len) {
        lwsl_err("ERROR %d writing to ws socket\n", m);
        return -1;
      }

      lws_ring_consume_and_update_oldest_tail(vhd->ring,           /* lws_ring object */
                                              MinnetBrokerSession, /* type of objects with tails */
                                              &pss->tail,          /* tail of guy doing the consuming */
                                              1,                   /* number of payload objects being consumed */
                                              vhd->pss_list,       /* head of list of objects with tails */
                                              tail,                /* member name of tail in objects with tails */
                                              pss_list             /* member name of next object in objects with tails */
      );

      /* more to do? */
      if(lws_ring_get_element(vhd->ring, &pss->tail))
        /* come back as soon as we can write more */
        lws_callback_on_writable(pss->wsi);
      break;

    case LWS_CALLBACK_RECEIVE:

      if(!pss->publishing)
        break;

      /* For test, our policy is ignore publishing when there are no subscribers connected. */
      if(!vhd->pss_list)
        break;

      n = (int)lws_ring_get_count_free_elements(vhd->ring);
      if(!n) {
        lwsl_user("dropping!\n");
        break;
      }

      amsg.len = len;
      /* notice we over-allocate by LWS_PRE */
      amsg.payload = malloc(LWS_PRE + len);
      if(!amsg.payload) {
        lwsl_user("OOM: dropping\n");
        break;
      }

      memcpy((char*)amsg.payload + LWS_PRE, in, len);
      if(!lws_ring_insert(vhd->ring, &amsg, 1)) {
        broker_destroy_message(&amsg);
        lwsl_user("dropping 2!\n");
        break;
      }

      /* let every subscriber know we want to write something on them as soon as they are ready */
      lws_start_foreach_llp(MinnetBrokerSession**, ppss, vhd->pss_list) {
        if(!(*ppss)->publishing)
          lws_callback_on_writable((*ppss)->wsi);
      }
      lws_end_foreach_llp(ppss, pss_list);
      break;

    default: break;
  }

  return 0;
}

#define MINNET_PLUGIN_BROKER(protocol_name) \
  { #protocol_name, broker_callback, sizeof(MinnetBrokerSession), 128, 0, NULL, 0 }
