#include "minnet.h"
#include "list.h"
#include <curl/curl.h>
#include <libwebsockets.h>
#include <sys/time.h>

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_minnet
#endif

void js_std_loop(JSContext *ctx);

__attribute__((visibility("default"))) JSModuleDef *
JS_INIT_MODULE(JSContext *ctx, const char *module_name)
{
	JSModuleDef *m;
	m = JS_NewCModule(ctx, module_name, js_minnet_init);
	if (!m)
		return NULL;
	JS_AddModuleExportList(ctx, m, minnet_funcs, countof(minnet_funcs));

	// Add class Response
	JS_NewClassID(&minnet_response_class_id);
	JS_NewClass(JS_GetRuntime(ctx), minnet_response_class_id,
				&minnet_response_class);
	JSValue response_proto = JS_NewObject(ctx);
	JS_SetPropertyFunctionList(ctx, response_proto, minnet_response_proto_funcs,
							   countof(minnet_response_proto_funcs));
	JS_SetClassProto(ctx, minnet_response_class_id, response_proto);

	// Add class WebSocket
	JS_NewClassID(&minnet_ws_class_id);
	JS_NewClass(JS_GetRuntime(ctx), minnet_ws_class_id, &minnet_ws_class);
	JSValue websocket_proto = JS_NewObject(ctx);
	JS_SetPropertyFunctionList(ctx, websocket_proto, minnet_ws_proto_funcs,
							   countof(minnet_ws_proto_funcs));
	JS_SetClassProto(ctx, minnet_ws_class_id, websocket_proto);

	return m;
}

#define GETCB(opt, cb_ptr)                                                     \
	if (JS_IsFunction(ctx, opt)) {                                             \
		struct minnet_ws_callback cb = {ctx, &this_val, &opt};                 \
		cb_ptr = cb;                                                           \
	}
#define SETLOG lws_set_log_level(LLL_ERR, NULL);

static JSValue create_websocket_obj(JSContext *ctx, struct lws *wsi)
{
	JSValue ws_obj = JS_NewObjectClass(ctx, minnet_ws_class_id);
	if (JS_IsException(ws_obj))
		return JS_EXCEPTION;

	MinnetWebsocket *res;
	res = js_mallocz(ctx, sizeof(*res));

	if (!res) {
		JS_FreeValue(ctx, ws_obj);
		return JS_EXCEPTION;
	}

	res->lwsi = wsi;
	JS_SetOpaque(ws_obj, res);
	return ws_obj;
}

static JSValue call_ws_callback(minnet_ws_callback *cb, int argc, JSValue *argv)
{
	return JS_Call(cb->ctx, *(cb->func_obj), *(cb->this_obj), argc, argv);
}

static JSValue minnet_service_handler(JSContext *ctx, JSValueConst this_val,
									  int argc, JSValueConst *argv, int magic,
									  JSValue *func_data)
{
	int32_t rw = 0;
	uint32_t calls = ++func_data[3].u.int32;
	struct lws_pollfd pfd;
	struct lws_pollargs args =
		*(struct lws_pollargs *)&JS_VALUE_GET_PTR(func_data[4]);
	struct lws_context *context = JS_VALUE_GET_PTR(func_data[2]);

	if (argc >= 1)
		JS_ToInt32(ctx, &rw, argv[0]);

	pfd.fd = JS_VALUE_GET_INT(func_data[0]);
	pfd.revents = rw ? POLLOUT : POLLIN;
	pfd.events = JS_VALUE_GET_INT(func_data[1]);

	if (pfd.events != (POLLIN | POLLOUT) || poll(&pfd, 1, 0) > 0)
		lws_service_fd(context, &pfd);

	/*if (calls <= 100)
		printf("minnet %s handler calls=%i fd=%d events=%d revents=%d pfd=[%d "
			   "%d %d]\n",
			   rw ? "writable" : "readable", calls, pfd.fd, pfd.events,
			   pfd.revents, args.fd, args.events, args.prev_events);*/

	return JS_UNDEFINED;
}

enum { READ_HANDLER = 0, WRITE_HANDLER };

static JSValue minnet_make_handler(JSContext *ctx, struct lws_pollargs *pfd,
								   struct lws *wsi, int magic)
{
	JSValue data[5] = {
		JS_MKVAL(JS_TAG_INT, pfd->fd),     JS_MKVAL(JS_TAG_INT, pfd->events),
		JS_MKPTR(0, lws_get_context(wsi)), JS_MKVAL(JS_TAG_INT, 0),
		JS_MKPTR(0, *(void **)pfd),
	};

	return JS_NewCFunctionData(ctx, minnet_service_handler, 0, magic,
							   countof(data), data);
}

static JSValue minnet_function_bound(JSContext *ctx, JSValueConst this_val,
									 int argc, JSValueConst argv[], int magic,
									 JSValue *func_data)
{
	JSValue args[argc + magic];
	size_t i, j;
	for (i = 0; i < magic; i++)
		args[i] = func_data[i + 1];
	for (j = 0; j < argc; j++)
		args[i++] = argv[j];

	return JS_Call(ctx, func_data[0], this_val, i, args);
}

static JSValue minnet_function_bind(JSContext *ctx, JSValueConst func, int argc,
									JSValueConst argv[])
{
	JSValue data[argc + 1];
	size_t i;
	data[0] = JS_DupValue(ctx, func);
	for (i = 0; i < argc; i++)
		data[i + 1] = JS_DupValue(ctx, argv[i]);
	return JS_NewCFunctionData(ctx, minnet_function_bound, 0, argc, argc + 1,
							   data);
}

static JSValue minnet_function_bind_1(JSContext *ctx, JSValueConst func,
									  JSValueConst arg)
{
	return minnet_function_bind(ctx, func, 1, &arg);
}

static void minnet_make_handlers(JSContext *ctx, struct lws *wsi,
								 struct lws_pollargs *pfd, JSValue out[2])
{
	JSValue func = minnet_make_handler(ctx, pfd, wsi, 0);

	out[0] =
		(pfd->events & POLLIN)
			? minnet_function_bind_1(ctx, func, JS_NewInt32(ctx, READ_HANDLER))
			: JS_NULL;
	out[1] =
		(pfd->events & POLLOUT)
			? minnet_function_bind_1(ctx, func, JS_NewInt32(ctx, WRITE_HANDLER))
			: JS_NULL;

	JS_FreeValue(ctx, func);
}

static int lws_ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
						   void *user, void *in, size_t len);

static int lws_http_callback(struct lws *wsi, enum lws_callback_reasons reason,
							 void *user, void *in, size_t len);

static struct lws_protocols lws_server_protocols[] = {
	{"minnet", lws_ws_callback, 0, 0},
	{"http", lws_http_callback, 0, 0},
	{NULL, NULL, 0, 0},
};

static int lws_http_callback(struct lws *wsi, enum lws_callback_reasons reason,
							 void *user, void *in, size_t len)
{
	// printf("http callback %d\n", reason);

	return lws_callback_http_dummy(wsi, reason, user, in, len);
}

static int lws_ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
						   void *user, void *in, size_t len)
{
	JSContext *ctx = lws_server_protocols[0].user;

	switch (reason) {
	case LWS_CALLBACK_PROTOCOL_INIT:
		printf("callback PROTOCOL_INIT\n");
		break;
	case LWS_CALLBACK_ESTABLISHED: {
		printf("callback ESTABLISHED\n");
		if (server_cb_connect.func_obj) {
			JSValue ws_obj = create_websocket_obj(server_cb_connect.ctx, wsi);
			call_ws_callback(&server_cb_connect, 1, &ws_obj);
		}
	} break;
	case LWS_CALLBACK_CLOSED: {
		// printf("callback CLOSED %d\n", lws_get_socket_fd(wsi));
		if (server_cb_close.func_obj) {
			call_ws_callback(&server_cb_close, 0, NULL);
		}
	} break;
	case LWS_CALLBACK_SERVER_WRITEABLE: {
		lws_callback_on_writable(wsi);
	} break;
	case LWS_CALLBACK_RECEIVE: {
		if (server_cb_message.func_obj) {
			JSValue ws_obj = create_websocket_obj(server_cb_message.ctx, wsi);
			JSValue msg = JS_NewStringLen(server_cb_message.ctx, in, len);
			JSValue cb_argv[2] = {ws_obj, msg};
			call_ws_callback(&server_cb_message, 2, cb_argv);
		}
	} break;
	case LWS_CALLBACK_RECEIVE_PONG: {
		if (server_cb_pong.func_obj) {
			JSValue ws_obj = create_websocket_obj(server_cb_pong.ctx, wsi);
			JSValue msg = JS_NewArrayBufferCopy(server_cb_pong.ctx, in, len);
			JSValue cb_argv[2] = {ws_obj, msg};
			call_ws_callback(&server_cb_pong, 2, cb_argv);
		}
	} break;
	case LWS_CALLBACK_HTTP_CONFIRM_UPGRADE: {
		// printf("callback HTTP_CONFIRM_UPGRADE fd=%d\n",
		// lws_get_socket_fd(wsi));
		break;
	}
	case LWS_CALLBACK_HTTP_BODY: {
		// printf("callback HTTP_BODY fd=%d\n", lws_get_socket_fd(wsi));
		break;
	}
	case LWS_CALLBACK_CLOSED_HTTP: {
		// printf("callback CLOSED_HTTP fd=%d\n", lws_get_socket_fd(wsi));
		break;
	}
	case LWS_CALLBACK_ADD_POLL_FD: {
		struct lws_pollargs *args = in;
		/*printf("callback ADD_POLL_FD fd=%d events=%s %s %s\n", args->fd,
			   (args->events & POLLIN) ? "IN" : "",
			   (args->events & POLLOUT) ? "OUT" : "",
			   (args->events & POLLERR) ? "ERR" : "");*/
		if (server_cb_fd.func_obj) {
			JSValue argv[3] = {JS_NewInt32(server_cb_fd.ctx, args->fd)};
			minnet_make_handlers(ctx, wsi, args, &argv[1]);

			call_ws_callback(&server_cb_fd, 3, argv);
			JS_FreeValue(ctx, argv[0]);
			JS_FreeValue(ctx, argv[1]);
			JS_FreeValue(ctx, argv[2]);
		}
		break;
	}
	case LWS_CALLBACK_DEL_POLL_FD: {
		struct lws_pollargs *args = in;
		// printf("callback DEL_POLL_FD fd=%d\n", args->fd);
		JSValue argv[3] = {
			JS_NewInt32(server_cb_fd.ctx, args->fd),
		};
		minnet_make_handlers(ctx, wsi, args, &argv[1]);
		call_ws_callback(&server_cb_fd, 3, argv);
		JS_FreeValue(ctx, argv[0]);

		break;
	}
	case LWS_CALLBACK_CHANGE_MODE_POLL_FD: {
		struct lws_pollargs *args = in;

		/*printf(
			"callback CHANGE_MODE_POLL_FD fd=%d events=%03o prev_events=%03o\n",
			args->fd, args->events, args->prev_events);*/

		if (args->events != args->prev_events) {
			JSValue argv[3] = {JS_NewInt32(server_cb_fd.ctx, args->fd)};
			minnet_make_handlers(ctx, wsi, args, &argv[1]);

			call_ws_callback(&server_cb_fd, 3, argv);
			JS_FreeValue(ctx, argv[0]);
			JS_FreeValue(ctx, argv[1]);
			JS_FreeValue(ctx, argv[2]);
		}
		break;
	}
	case LWS_CALLBACK_HTTP_FILE_COMPLETION: {
		// printf("callback HTTP_FILE_COMPLETION\n");
		break;
	}
	case LWS_CALLBACK_FILTER_NETWORK_CONNECTION: {
		// printf("callback FILTER_NETWORK_CONNECTION\n");
		break;
	}
	case LWS_CALLBACK_FILTER_HTTP_CONNECTION: {
		printf("callback FILTER_HTTP_CONNECTION\n");
		break;
	}
	case LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED: {
		// printf("callback SERVER_NEW_CLIENT_INSTANTIATED\n");
		break;
	}
	case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS: {
		// printf("callback OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS\n");
		break;
	}
	case LWS_CALLBACK_WSI_CREATE: {
		printf("callback WSI_CREATE\n");
		break;
	}
	case LWS_CALLBACK_LOCK_POLL: {
		break;
	}
	case LWS_CALLBACK_UNLOCK_POLL: {
		break;
	}
	case LWS_CALLBACK_HTTP_BIND_PROTOCOL: {
		// printf("callback HTTP_BIND_PROTOCOL\n");
		break;
	}
	case LWS_CALLBACK_HTTP_DROP_PROTOCOL: {
		// printf("callback HTTP_DROP_PROTOCOL\n");
		break;
	}
	case LWS_CALLBACK_WSI_DESTROY: {
		// printf("callback LWS_CALLBACK_WSI_DESTROY %d\n",
		// lws_get_socket_fd(in));
		break;
	}

	default:
		// printf("Unknown lws callback %d\n", reason);
		break;
	}

	return lws_callback_http_dummy(wsi, reason, user, in, len);
}

static struct lws_http_mount *minnet_get_mount(JSContext *ctx, JSValueConst arr)
{
	JSValue mountpoint = JS_GetPropertyUint32(ctx, arr, 0);
	JSValue origin = JS_GetPropertyUint32(ctx, arr, 1);
	JSValue def = JS_GetPropertyUint32(ctx, arr, 2);
	struct lws_http_mount *ret = js_mallocz(ctx, sizeof(struct lws_http_mount));
	const char *dest = JS_ToCString(ctx, origin);
	const char *dotslashslash = strstr(dest, "://");
	size_t proto_len = dotslashslash ? dotslashslash - dest : 0;

	ret->mountpoint = JS_ToCString(ctx, mountpoint);
	ret->origin = js_strdup(ctx, &dest[proto_len ? proto_len + 3 : 0]);
	ret->def = JS_IsUndefined(def) ? 0 : JS_ToCString(ctx, def);
	ret->origin_protocol =
		proto_len == 0
			? LWSMPRO_FILE
			: !strncmp(dest, "https", proto_len) ? LWSMPRO_HTTPS : LWSMPRO_HTTP;
	ret->mountpoint_len = strlen(ret->mountpoint);

	JS_FreeCString(ctx, dest);

	return ret;
}

static void minnet_free_mount(JSContext *ctx, struct lws_http_mount *mount)
{
	JS_FreeCString(ctx, mount->mountpoint);
	js_free(ctx, (char *)mount->origin);
	if (mount->def)
		JS_FreeCString(ctx, mount->def);
	js_free(ctx, mount);
}
typedef struct JSThreadState {
	struct list_head os_rw_handlers;
	struct list_head os_signal_handlers;
	struct list_head os_timers;
	struct list_head port_list;
	int eval_script_recurse;
	void *recv_pipe, *send_pipe;
} JSThreadState;

/*static int minnet_ws_service(struct lws_context *context, uint32_t
timeout)
{
	int ret, i, j = 0, n = FD_SETSIZE;
	struct pollfd pfds[n];
	struct lws *wss[n];

	for (i = 0; i < n; i++) {
		if ((wss[j] = wsi_from_fd(context, i))) {
			printf("wss[%d] (%d) %i\n", j, i,
lws_partial_buffered(wss[j]));

			pfds[j] = (struct pollfd){
				.fd = i,
				.events = POLLIN | (lws_partial_buffered(wss[j]) ?
POLLOUT : 0), .revents = 0}; j++;
		}
	}

	if ((ret = poll(pfds, j, timeout)) != -1) {
		for (i = 0; i < j; i++) {
			lws_service_fd(context, (struct lws_pollfd *)&pfds[i]);
		}
	}
	return ret;
}*/

static JSValue minnet_ws_server(JSContext *ctx, JSValueConst this_val, int argc,
								JSValueConst *argv)
{
	int a = 0;
	int port = 7981;
	const char *host;
	struct lws_context *context;
	struct lws_context_creation_info info;

	JSValue options = argv[0];

	JSValue opt_port = JS_GetPropertyStr(ctx, options, "port");
	JSValue opt_host = JS_GetPropertyStr(ctx, options, "host");
	JSValue opt_on_pong = JS_GetPropertyStr(ctx, options, "onPong");
	JSValue opt_on_close = JS_GetPropertyStr(ctx, options, "onClose");
	JSValue opt_on_connect = JS_GetPropertyStr(ctx, options, "onConnect");
	JSValue opt_on_message = JS_GetPropertyStr(ctx, options, "onMessage");
	JSValue opt_on_fd = JS_GetPropertyStr(ctx, options, "onFd");
	JSValue opt_mounts = JS_GetPropertyStr(ctx, options, "mounts");

	if (JS_IsNumber(opt_port))
		JS_ToInt32(ctx, &port, opt_port);

	if (JS_IsString(opt_host))
		host = JS_ToCString(ctx, opt_host);
	else
		host = "localhost";

	GETCB(opt_on_pong, server_cb_pong)
	GETCB(opt_on_close, server_cb_close)
	GETCB(opt_on_connect, server_cb_connect)
	GETCB(opt_on_message, server_cb_message)
	GETCB(opt_on_fd, server_cb_fd)

	SETLOG

	memset(&info, 0, sizeof info);

	lws_server_protocols[0].user = ctx;
	lws_server_protocols[1].user = ctx;

	info.port = port;
	info.protocols = lws_server_protocols;
	info.mounts = 0;
	info.vhost_name = host;
	info.options =
		LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT |
		LWS_SERVER_OPTION_HTTP_HEADERS_SECURITY_BEST_PRACTICES_ENFORCE;

	if (JS_IsArray(ctx, opt_mounts)) {
		const struct lws_http_mount **ptr = &info.mounts;
		uint32_t i;

		for (i = 0;; i++) {
			JSValue mount = JS_GetPropertyUint32(ctx, opt_mounts, i);

			if (JS_IsUndefined(mount))
				break;

			*ptr = minnet_get_mount(ctx, mount);
			ptr = (const struct lws_http_mount **)&(*ptr)->mount_next;
		}
	}

	context = lws_create_context(&info);

	if (!context) {
		lwsl_err("Libwebsockets init failed\n");
		return JS_EXCEPTION;
	}

	JSThreadState *ts = JS_GetRuntimeOpaque(JS_GetRuntime(ctx));
	lws_service_adjust_timeout(context, 1, 0);

	while (a >= 0) {
		if (server_cb_fd.func_obj)
			js_std_loop(ctx);
		else
			a = lws_service(context, 20);
	}
	lws_context_destroy(context);

	if (info.mounts) {
		const struct lws_http_mount *mount, *next;

		for (mount = info.mounts; mount; mount = next) {
			next = mount->mount_next;
			JS_FreeCString(ctx, mount->mountpoint);
			JS_FreeCString(ctx, mount->origin);
			if (mount->def)
				JS_FreeCString(ctx, mount->def);
			js_free(ctx, (void *)mount);
		}
	}

	return JS_NewInt32(ctx, 0);
}

static struct lws_context *client_context;
static struct lws *client_wsi;
static int port = 7981;
static const char *client_server_address = "localhost";

static int connect_client(void)
{
	struct lws_client_connect_info i;

	memset(&i, 0, sizeof(i));

	i.context = client_context;
	i.port = port;
	i.address = client_server_address;
	i.path = "/";
	i.host = i.address;
	i.origin = i.address;
	i.ssl_connection = 0;
	i.protocol = "minnet";
	i.pwsi = &client_wsi;

	return !lws_client_connect_via_info(&i);
}

static int lws_client_callback(struct lws *wsi,
							   enum lws_callback_reasons reason, void *user,
							   void *in, size_t len)
{
	switch (reason) {
	case LWS_CALLBACK_PROTOCOL_INIT: {
		connect_client();
	} break;
	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
		client_wsi = NULL;
		if (client_cb_close.func_obj) {
			JSValue why = JS_NewString(client_cb_close.ctx, in);
			call_ws_callback(&client_cb_close, 1, &why);
		}
	} break;
	case LWS_CALLBACK_CLIENT_ESTABLISHED: {
		if (client_cb_connect.func_obj) {
			JSValue ws_obj = create_websocket_obj(client_cb_connect.ctx, wsi);
			call_ws_callback(&client_cb_connect, 1, &ws_obj);
		}
	} break;
	case LWS_CALLBACK_CLIENT_WRITEABLE: {
		lws_callback_on_writable(wsi);
	} break;
	case LWS_CALLBACK_WS_CLIENT_DROP_PROTOCOL: {
		client_wsi = NULL;
		if (client_cb_close.func_obj) {
			JSValue why = JS_NewString(client_cb_close.ctx, in);
			call_ws_callback(&client_cb_close, 1, &why);
		}
	} break;
	case LWS_CALLBACK_CLIENT_RECEIVE: {
		if (client_cb_message.func_obj) {
			JSValue ws_obj = create_websocket_obj(client_cb_message.ctx, wsi);
			JSValue msg = JS_NewStringLen(client_cb_message.ctx, in, len);
			JSValue cb_argv[2] = {ws_obj, msg};
			call_ws_callback(&client_cb_message, 2, cb_argv);
		}
	} break;
	case LWS_CALLBACK_CLIENT_RECEIVE_PONG: {
		if (client_cb_pong.func_obj) {
			JSValue ws_obj = create_websocket_obj(client_cb_pong.ctx, wsi);
			JSValue data = JS_NewArrayBufferCopy(client_cb_pong.ctx, in, len);
			JSValue cb_argv[2] = {ws_obj, data};
			call_ws_callback(&client_cb_pong, 2, cb_argv);
		}
	} break;
	default:
		break;
	}

	return lws_callback_http_dummy(wsi, reason, user, in, len);
}

static const struct lws_protocols client_protocols[] = {
	{"minnet", lws_client_callback, 0, 0},
	{NULL, NULL, 0, 0},
};

static JSValue minnet_ws_client(JSContext *ctx, JSValueConst this_val, int argc,
								JSValueConst *argv)
{
	struct lws_context_creation_info info;
	int n = 0;

	SETLOG

	memset(&info, 0, sizeof info);
	info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
	info.port = CONTEXT_PORT_NO_LISTEN;
	info.protocols = client_protocols;

	JSValue options = argv[0];
	JSValue opt_port = JS_GetPropertyStr(ctx, options, "port");
	JSValue opt_host = JS_GetPropertyStr(ctx, options, "host");
	JSValue opt_on_pong = JS_GetPropertyStr(ctx, options, "onPong");
	JSValue opt_on_close = JS_GetPropertyStr(ctx, options, "onClose");
	JSValue opt_on_connect = JS_GetPropertyStr(ctx, options, "onConnect");
	JSValue opt_on_message = JS_GetPropertyStr(ctx, options, "onMessage");

	if (JS_IsString(opt_host))
		client_server_address = JS_ToCString(ctx, opt_host);

	if (JS_IsNumber(opt_port))
		JS_ToInt32(ctx, &port, opt_port);

	GETCB(opt_on_pong, client_cb_pong)
	GETCB(opt_on_close, client_cb_close)
	GETCB(opt_on_connect, client_cb_connect)
	GETCB(opt_on_message, client_cb_message)

	client_context = lws_create_context(&info);
	if (!client_context) {
		lwsl_err("Libwebsockets init failed\n");
		return JS_EXCEPTION;
	}

	while (n >= 0) {
		n = lws_service(client_context, 500);
		js_std_loop(ctx);
	}

	lws_context_destroy(client_context);

	return JS_EXCEPTION;
}

static JSValue minnet_ws_send(JSContext *ctx, JSValueConst this_val, int argc,
							  JSValueConst *argv)
{
	MinnetWebsocket *ws_obj;
	const char *msg;
	uint8_t *data;
	size_t len;
	int m, n;

	ws_obj = JS_GetOpaque(this_val, minnet_ws_class_id);
	if (!ws_obj)
		return JS_EXCEPTION;

	if (JS_IsString(argv[0])) {
		msg = JS_ToCString(ctx, argv[0]);
		len = strlen(msg);
		uint8_t buffer[LWS_PRE + len];

		n = lws_snprintf((char *)&buffer[LWS_PRE], len + 1, "%s", msg);
		m = lws_write(ws_obj->lwsi, &buffer[LWS_PRE], len, LWS_WRITE_TEXT);
		if (m < n) {
			// Sending message failed
			return JS_EXCEPTION;
		}
		return JS_UNDEFINED;
	}

	data = JS_GetArrayBuffer(ctx, &len, argv[0]);
	if (data) {
		uint8_t buffer[LWS_PRE + len];
		memcpy(&buffer[LWS_PRE], data, len);

		m = lws_write(ws_obj->lwsi, &buffer[LWS_PRE], len, LWS_WRITE_BINARY);
		if (m < len) {
			// Sending data failed
			return JS_EXCEPTION;
		}
	}
	return JS_UNDEFINED;
}

static JSValue minnet_ws_ping(JSContext *ctx, JSValueConst this_val, int argc,
							  JSValueConst *argv)
{
	MinnetWebsocket *ws_obj;
	uint8_t *data;
	size_t len;

	ws_obj = JS_GetOpaque(this_val, minnet_ws_class_id);
	if (!ws_obj)
		return JS_EXCEPTION;

	data = JS_GetArrayBuffer(ctx, &len, argv[0]);
	if (data) {
		uint8_t buffer[len + LWS_PRE];
		memcpy(&buffer[LWS_PRE], data, len);

		int m = lws_write(ws_obj->lwsi, &buffer[LWS_PRE], len, LWS_WRITE_PING);
		if (m < len) {
			// Sending ping failed
			return JS_EXCEPTION;
		}
	} else {
		uint8_t buffer[LWS_PRE];
		lws_write(ws_obj->lwsi, &buffer[LWS_PRE], 0, LWS_WRITE_PING);
	}
	return JS_UNDEFINED;
}

static JSValue minnet_ws_pong(JSContext *ctx, JSValueConst this_val, int argc,
							  JSValueConst *argv)
{
	MinnetWebsocket *ws_obj;
	uint8_t *data;
	size_t len;

	ws_obj = JS_GetOpaque(this_val, minnet_ws_class_id);
	if (!ws_obj)
		return JS_EXCEPTION;

	data = JS_GetArrayBuffer(ctx, &len, argv[0]);
	if (data) {
		uint8_t buffer[len + LWS_PRE];
		memcpy(&buffer[LWS_PRE], data, len);

		int m = lws_write(ws_obj->lwsi, &buffer[LWS_PRE], len, LWS_WRITE_PONG);
		if (m < len) {
			// Sending pong failed
			return JS_EXCEPTION;
		}
	} else {
		uint8_t buffer[LWS_PRE];
		lws_write(ws_obj->lwsi, &buffer[LWS_PRE], 0, LWS_WRITE_PONG);
	}
	return JS_UNDEFINED;
}

static JSValue minnet_ws_close(JSContext *ctx, JSValueConst this_val, int argc,
							   JSValueConst *argv)
{
	MinnetWebsocket *ws_obj;

	ws_obj = JS_GetOpaque(this_val, minnet_ws_class_id);
	if (!ws_obj)
		return JS_EXCEPTION;

	// TODO: Find out how to clsoe connection

	return JS_UNDEFINED;
}

static JSValue minnet_ws_get(JSContext *ctx, JSValueConst this_val, int magic)
{
	MinnetWebsocket *ws_obj;

	if (!(ws_obj = JS_GetOpaque(this_val, minnet_ws_class_id)))
		return JS_EXCEPTION;

	return JS_NewInt32(ctx, lws_get_socket_fd(ws_obj->lwsi));
}

static void minnet_ws_finalizer(JSRuntime *rt, JSValue val)
{
	MinnetWebsocket *ws_obj = JS_GetOpaque(val, minnet_ws_class_id);
	if (ws_obj)
		js_free_rt(rt, ws_obj);
}

static JSValue minnet_fetch(JSContext *ctx, JSValueConst this_val, int argc,
							JSValueConst *argv)
{
	CURL *curl;
	CURLcode curlRes;
	const char *url;
	FILE *fi;
	MinnetResponse *res;
	uint8_t *buffer;
	long bufSize;
	long status;
	char *type;
	const char *body_str = NULL;
	struct curl_slist *headerlist = NULL;
	char *buf = calloc(1, 1);
	size_t bufsize = 1;

	JSValue resObj = JS_NewObjectClass(ctx, minnet_response_class_id);
	if (JS_IsException(resObj))
		return JS_EXCEPTION;

	res = js_mallocz(ctx, sizeof(*res));

	if (!res) {
		JS_FreeValue(ctx, resObj);
		return JS_EXCEPTION;
	}

	if (!JS_IsString(argv[0]))
		return JS_EXCEPTION;

	res->url = argv[0];
	url = JS_ToCString(ctx, argv[0]);

	if (argc > 1 && JS_IsObject(argv[1])) {
		JSValue method, body, headers;
		const char *method_str;
		method = JS_GetPropertyStr(ctx, argv[1], "method");
		body = JS_GetPropertyStr(ctx, argv[1], "body");
		headers = JS_GetPropertyStr(ctx, argv[1], "headers");

		if (!JS_IsUndefined(headers)) {
			JSValue global_obj, object_ctor, /* object_proto, */ keys, names,
				length;
			int i;
			int32_t len;

			global_obj = JS_GetGlobalObject(ctx);
			object_ctor = JS_GetPropertyStr(ctx, global_obj, "Object");
			keys = JS_GetPropertyStr(ctx, object_ctor, "keys");

			names =
				JS_Call(ctx, keys, object_ctor, 1, (JSValueConst *)&headers);
			length = JS_GetPropertyStr(ctx, names, "length");

			JS_ToInt32(ctx, &len, length);

			for (i = 0; i < len; i++) {
				char *h;
				JSValue key, value;
				const char *key_str, *value_str;
				size_t key_len, value_len;
				key = JS_GetPropertyUint32(ctx, names, i);
				key_str = JS_ToCString(ctx, key);
				key_len = strlen(key_str);

				value = JS_GetPropertyStr(ctx, headers, key_str);
				value_str = JS_ToCString(ctx, value);
				value_len = strlen(value_str);

				buf = realloc(buf, bufsize + key_len + 2 + value_len + 2 + 1);
				h = &buf[bufsize];

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

				headerlist = curl_slist_append(headerlist, h);
			}

			JS_FreeValue(ctx, global_obj);
			JS_FreeValue(ctx, object_ctor);
			// JS_FreeValue(ctx, object_proto);
			JS_FreeValue(ctx, keys);
			JS_FreeValue(ctx, names);
			JS_FreeValue(ctx, length);
		}

		method_str = JS_ToCString(ctx, method);

		if (!JS_IsUndefined(body) || !strcasecmp(method_str, "post")) {
			body_str = JS_ToCString(ctx, body);
		}

		JS_FreeCString(ctx, method_str);

		JS_FreeValue(ctx, method);
		JS_FreeValue(ctx, body);
		JS_FreeValue(ctx, headers);
	}

	curl = curl_easy_init();
	if (!curl)
		return JS_EXCEPTION;

	fi = tmpfile();

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "minimal-network-quickjs");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fi);

	if (body_str)
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str);

	curlRes = curl_easy_perform(curl);
	if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK)
		res->status = JS_NewInt32(ctx, (int32_t)status);

	if (curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &type) == CURLE_OK)
		res->type = type ? JS_NewString(ctx, type) : JS_NULL;

	res->ok = JS_FALSE;

	if (curlRes != CURLE_OK) {
		fprintf(stderr, "CURL failed: %s\n", curl_easy_strerror(curlRes));
		goto finish;
	}

	bufSize = ftell(fi);
	rewind(fi);

	buffer = calloc(1, bufSize + 1);
	if (!buffer) {
		fclose(fi), fputs("memory alloc fails", stderr);
		goto finish;
	}

	/* copy the file into the buffer */
	if (1 != fread(buffer, bufSize, 1, fi)) {
		fclose(fi), free(buffer), fputs("entire read fails", stderr);
		goto finish;
	}

	fclose(fi);

	res->ok = JS_TRUE;
	res->buffer = buffer;
	res->size = bufSize;

finish:
	curl_slist_free_all(headerlist);
	free(buf);
	if (body_str)
		JS_FreeCString(ctx, body_str);

	curl_easy_cleanup(curl);
	JS_SetOpaque(resObj, res);

	return resObj;
}

static JSValue minnet_response_buffer(JSContext *ctx, JSValueConst this_val,
									  int argc, JSValueConst *argv)
{
	MinnetResponse *res = JS_GetOpaque(this_val, minnet_response_class_id);
	if (res && res->buffer) {
		JSValue val = JS_NewArrayBufferCopy(ctx, res->buffer, res->size);
		return val;
	}

	return JS_EXCEPTION;
}

static JSValue minnet_response_json(JSContext *ctx, JSValueConst this_val,
									int argc, JSValueConst *argv)
{
	MinnetResponse *res = JS_GetOpaque(this_val, minnet_response_class_id);
	if (res && res->buffer)
		return JS_ParseJSON(ctx, (char *)res->buffer, res->size, "<input>");

	return JS_EXCEPTION;
}

static JSValue minnet_response_text(JSContext *ctx, JSValueConst this_val,
									int argc, JSValueConst *argv)
{
	MinnetResponse *res = JS_GetOpaque(this_val, minnet_response_class_id);
	if (res && res->buffer)
		return JS_NewStringLen(ctx, (char *)res->buffer, res->size);

	return JS_EXCEPTION;
}

static JSValue minnet_response_getter_ok(JSContext *ctx, JSValueConst this_val)
{
	MinnetResponse *res = JS_GetOpaque(this_val, minnet_response_class_id);
	if (res)
		return res->ok;

	return JS_EXCEPTION;
}

static JSValue minnet_response_getter_url(JSContext *ctx, JSValueConst this_val)
{
	MinnetResponse *res = JS_GetOpaque(this_val, minnet_response_class_id);
	if (res)
		return res->url;

	return JS_EXCEPTION;
}

static JSValue minnet_response_getter_status(JSContext *ctx,
											 JSValueConst this_val)
{
	MinnetResponse *res = JS_GetOpaque(this_val, minnet_response_class_id);
	if (res)
		return res->status;

	return JS_EXCEPTION;
}

static JSValue minnet_response_getter_type(JSContext *ctx,
										   JSValueConst this_val)
{
	MinnetResponse *res = JS_GetOpaque(this_val, minnet_response_class_id);
	if (res) {
		return res->type;
	}

	return JS_EXCEPTION;
}

static void minnet_response_finalizer(JSRuntime *rt, JSValue val)
{
	MinnetResponse *res = JS_GetOpaque(val, minnet_response_class_id);
	if (res) {
		if (res->buffer)
			free(res->buffer);
		js_free_rt(rt, res);
	}
}