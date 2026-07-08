# qjs-net

Networking module for [QuickJS](https://bellard.org/quickjs/), built on [libwebsockets](https://libwebsockets.org/).
(Derived from [minnet-quickjs](https://github.com/khanhas/minnet-quickjs))

It provides:

- WebSocket / HTTP / HTTPS / raw socket **server** (`createServer`, `Server`)
- WebSocket / HTTP / HTTPS / raw socket **client** (`client`, `Client`)
- a **fetch()** style HTTP request function
- helper classes: `Socket`, `Request`, `Response`, `Headers`, `URL`, `Generator`, `AsyncIterator`, `Ringbuffer`, `FormParser`, `Hash`
- utility functions: `setLog`, `getSessions`, `generateCert`

## Building

Requirements:

- `clang` or `gcc`
- `cmake`
- `libwebsockets` (bundled as a git submodule)
- OpenSSL (or LibreSSL)

```bash
git clone https://github.com/rsenn/qjs-net
cd qjs-net
git submodule update --init
cd build
cmake ..
make
```

Alternatively use `premake5 gmake` or `ninja -C build`. These alternative build
methods can't invoke the compilation of `libwebsockets`, so build and install it
manually first:

```bash
cd qjs-net
. build-libwebsockets.sh

TYPE=Release builddir=libwebsockets/build build_libwebsockets

make -C libwebsockets/build install
```

You may prepend `OPENSSL_PREFIX` (e.g. `OPENSSL_PREFIX=/opt/libressl-3.5.1`)
when building against a custom SSL library.

## Usage

```javascript
import * as net from 'net.so';
// or import individual exports:
import { createServer, client, fetch, Socket, Response, URL, Hash } from 'net.so';
```

---

# Module functions

## `createServer(options)`

Creates a WebSocket/HTTP server. Returns a `Server` instance (or a Promise when
run in non-blocking mode).

`options` object:

| Property | Type | Description |
|---|---|---|
| `port` | number | Port to listen on |
| `host` | string | Hostname/interface to bind |
| `protocol` | string | Protocol: `"ws"`, `"http"`, `"https"`, `"raw"`, … |
| `tls` | boolean | Enable TLS |
| `sslCert` | string/ArrayBuffer | Server certificate (path or PEM data) |
| `sslPrivateKey` | string/ArrayBuffer | Private key (path or PEM data) |
| `sslCA` | string/ArrayBuffer | CA certificate |
| `mounts` | object/array | HTTP mounts: maps URL paths to directories, callback functions or proxies (see below) |
| `mimetypes` | array | Additional MIME type mappings `[[".ext", "type/subtype"], …]` |
| `errorDocument` | string | Document served on HTTP errors |
| `options` | object | Extra per-vhost options |
| `block` | boolean | Blocking mode: when `false`, returns a Promise and serving is driven by the event loop |
| `onConnect(socket)` | function | A client connected; receives the `Socket` |
| `onClose(socket, reason)` | function | A client disconnected |
| `onMessage(socket, data)` | function | WebSocket message received |
| `onPong(socket, data)` | function | Pong frame received |
| `onRequest(request, response)` | function | HTTP request received |
| `onPost(request, data)` | function | HTTP POST body received |
| `onRead(...)` | function | Raw socket data readable |
| `onFd(fd, readHandler, writeHandler)` | function | Poll-fd bookkeeping; used to integrate with the runtime's event loop (defaults to `os.setReadHandler`/`os.setWriteHandler`) |
| `onCheckAccessRights(...)` | function | Access control hook |
| `onCertificateVerify(...)` | function | TLS peer certificate verification hook |

Mounts example:

```javascript
createServer({
  port: 8765,
  mounts: {
    '/': ['/', '.', 'index.html'],        // serve current directory
    '/proxy': ['/proxy', 'https://example.com/', null], // reverse proxy
    *generator(req, res) {                 // dynamic response from a generator
      yield 'Hello ';
      yield 'World';
    },
  },
  onRequest(req, res) {
    console.log(req.method, req.url.path);
  },
});
```

## `client(url[, options])`

Creates a WebSocket/HTTP/raw client and connects. Returns a `Client` instance
(blocking mode) or a Promise.

`options` object:

| Property | Type | Description |
|---|---|---|
| `protocol` | string | Protocol override (`"ws"`, `"http"`, `"raw"`, …) |
| `method` | string | HTTP method for HTTP clients |
| `headers` | object | Request headers |
| `body` | string/ArrayBuffer/iterator | Request body |
| `block` | boolean | Blocking (synchronous) operation; when `false` a Promise is returned |
| `binary` | boolean | Deliver messages as `ArrayBuffer` instead of string |
| `lineBuffered` | boolean | Split received data into lines |
| `buffering` | number | Buffer size for buffered reading |
| `tls` | boolean | Enable TLS |
| `sslCert`, `sslPrivateKey`, `sslCA` | string/ArrayBuffer | Client TLS credentials |
| `onConnect(socket)` | function | Connection established |
| `onClose(socket, reason)` | function | Connection closed |
| `onError(socket, error)` | function | Connection error |
| `onMessage(socket, data)` | function | Message received |
| `onPong(socket, data)` | function | Pong received |
| `onHttp(request, response)` | function | HTTP response received |
| `onFd(fd, readHandler, writeHandler)` | function | Event loop integration (see `createServer`) |

A blocking `Client` is synchronously iterable, a non-blocking one is async
iterable — iteration yields received messages:

```javascript
for await (let msg of net.client('wss://echo.example.org')) {
  console.log(msg);
}
```

## `fetch(url[, options])`

Requests an HTTP resource. Accepts the same `options` as `client()` (notably
`method`, `headers`, `body`, `tls`). Operates in blocking mode by default and
returns a `Response`; with `{ block: false }` it returns a
`Promise<Response>`.

```javascript
const res = net.fetch('https://www.example.com/');
console.log(res.status, res.text());
```

## `getSessions()`

Returns an array of all currently tracked sessions (session objects, `Socket`
instances or serial numbers) across servers and clients.

## `setLog([level, ]callback[, thisObj])`

Sets the libwebsockets log level and log callback. Returns the previous
callback.

- `level` — bitmask of `LLL_*` flags (see constants below)
- `callback(level, message)` — invoked for every log line

```javascript
net.setLog(net.LLL_ERR | net.LLL_WARN, (level, msg) => console.log(net.logLevels[level], msg));
```

## `generateCert([options])`

Generates a self-signed RSA certificate. Returns
`{ cert: ArrayBuffer, key: ArrayBuffer, commonName: string }` — PEM-encoded
certificate and private key, usable directly as `sslCert` / `sslPrivateKey`.

`options`:

- `commonName` — subject CN (default `"localhost"`)
- `bits` — RSA key size (default `2048`)
- `days` — validity in days (default `365`)
- `altNames` — array of subject alternative names (DNS names or IP literals)

---

# Classes

## `Server`

Created by `createServer()`; not constructible directly.

Methods:

- `listen([port])` — start listening (optionally overriding the port)
- `get([path, ]handler)` — register a handler for GET requests matching `path`
- `post([path, ]handler)` — register a handler for POST requests
- `use([path, ]handler)` — register a handler for all methods
- `mount(path, origin[, default[, protocol]])` / `mount(obj)` — add an HTTP mount

Properties:

- `onrequest` — get/set the HTTP request callback
- `listening` — *read-only* boolean

## `Client`

Created by `client()`; not constructible directly.

Properties (read-only): `request`, `response`, `socket`.

Callback properties (get/set): `onmessage`, `onconnect`, `onclose`, `onerror`,
`onpong`, `onfd`, `onhttp`, `onwriteable`.

Other: `lineBuffered` (get/set). Instances are iterable (blocking mode) or
async-iterable (non-blocking mode), yielding received messages.

## `Socket`

Represents a WebSocket / HTTP / raw connection. Instances are passed to server
and client callbacks; the export is not constructible.

Methods:

- `send(data[, writeFlags])` — send a message; strings are sent as text frames,
  ArrayBuffers as binary frames. `writeFlags` is one of the `LWS_WRITE_*`
  constants. Returns the number of bytes written.
- `ping([data])` — send a ping frame (`data`: ArrayBuffer)
- `pong([data])` — send a pong frame (`data`: ArrayBuffer)
- `close([status[, reason]])` — close the connection (`status`: one of the
  `CLOSE_STATUS_*` constants, default `CLOSE_STATUS_NORMAL`)
- `respond(status[, message])` — (HTTP) return an HTTP status response
- `redirect(status, location)` — (HTTP) send an HTTP redirect
- `header(name, value)` — (HTTP) add a response header

Properties (read-only unless noted):

- `protocol` — negotiated protocol name
- `fd` — socket file descriptor
- `address` — peer address string
- `family` — address family (`AF_INET`, …)
- `port` — peer port
- `local` — local address as `[host, port]`
- `peer` — peer address as `[host, port]`
- `tls` — whether the connection uses TLS
- `bufferedAmount` — bytes queued but not yet sent
- `raw` — whether this is a raw (non-WebSocket) connection
- `binary` — *get/set* — binary message delivery
- `readyState` — `CONNECTING` (0), `OPEN` (1), `CLOSING` (2) or `CLOSED` (3)
- `serial` — unique connection serial number

Constants (on the class and prototype): `CONNECTING`, `OPEN`, `CLOSING`,
`CLOSED`, `CLOSE_STATUS_*` (WebSocket close codes) and `HTTP_STATUS_*` (HTTP
status codes).

## `Request`

HTTP request object, passed to `onRequest`/`onPost` handlers.

- `new Request(url[, options])` — create a request; `url` is a string, `URL` or
  another `Request` (which is copied). String properties of `options` are
  copied onto the object.

Methods:

- `arrayBuffer()` — resolves with the request body as `ArrayBuffer`
- `text()` — resolves with the request body as string
- `json()` — resolves with the parsed request body
- `get(name)` — get a single request header
- `clone()` — duplicate the request

Properties: `type`, `url` *(get/set)*, `method` *(get/set)*, `path` *(get/set)*,
`protocol`, `headers` *(get/set)*, `referer`, `body`, `secure` *(read-only)*,
`h2` *(read-only)*.

## `Response`

HTTP response object; also what `fetch()` returns.

- `new Response([body[, options]])` — create a response with the given body;
  string properties of `options` (e.g. `status`, `headers`) are copied onto the
  object.

Methods:

- `text()` / `json()` / `arrayBuffer()` — get the body as string / parsed JSON /
  `ArrayBuffer` (async in non-blocking mode)
- `write(data)` — append data to the response body
- `finish()` — end the response body
- `get(name)` / `set(name, value)` / `append(name, value)` — read/modify headers
- `location(url)` — set the `Location` header
- `clone()` — duplicate the response
- `[Symbol.asyncIterator]()` — iterate over body chunks

Properties: `status` *(get/set)*, `statusText` *(get/set)*, `ok`, `url`
*(get/set)*, `type`, `headers` *(get/set)*, `headersSent`, `redirected`
*(get/set)*, `body` *(get/set)*, `bodyUsed`.

Static methods:

- `Response.redirect(url[, status])` — create a redirect response
- `Response.error()` — create an error response

## `Headers`

HTTP header collection, exposed via `Request.prototype.headers` /
`Response.prototype.headers`.

Methods: `get(name)`, `set(name, value)`, `has(name)`, `append(name, value)`,
`delete(name)`, `keys()`, `values()`, `entries()`.

Properties: `buffer` — underlying buffer (read-only).

## `URL`

URL parser/builder.

- `new URL(string)` — parse a URL string
- `URL.from(value)` — static; create a `URL` from a string or object

Properties: `protocol`, `hostname`, `port`, `pathname`, `query` *(all
get/set)*; `host`, `path`, `search`, `hash`, `origin`, `tls`, `href` *(read-
only)*.

Methods: `toString()`, `toObject()`, `inspect()`.

## `Generator`

Push-based async generator used for streaming bodies (e.g. HTTP responses).

- `new Generator(executor[, bufferSize])` — `executor(push, stop)` is a function
  receiving a `push(value)` callback to emit chunks and a `stop([error])`
  callback to end the stream.

Methods: `write(data)`, `enqueue(data)`, `continuous()`, `buffering([size])`,
`stop()`, `[Symbol.asyncIterator]()`.

Properties (read-only): `isStarted`, `isStopped`, `isContinuous`,
`isBuffering`, `bytesRead`, `bytesWritten`, `chunksRead`, `chunksWritten`,
`chunkSize`.

## `AsyncIterator`

Minimal push-driven async iterator.

- `new AsyncIterator()`

Methods:

- `next([value])` — returns a Promise for the next pushed value
- `push(value)` — yield a value to a pending `next()`; returns boolean
- `stop([value])` — end the iteration; returns boolean

## `Ringbuffer`

Fixed-size multi-tail ring buffer (wraps `lws_ring`).

- `new Ringbuffer([type, ]elementSize, count)` — `type` is an optional element
  type name string.

Methods:

- `insert(data)` — insert element(s), returns count inserted
- `consume([count])` — consume element(s) from a tail
- `skip([count])` — skip elements
- `getElement()` — peek the next element
- `getWaitingElements()` — number of elements waiting
- `getConsumeRange()` — linear consume range
- `createTail()` — create an additional consumer tail
- `bumpHead(bytes)` — advance the head pointer
- `updateOldestTail(offset)` — move the oldest tail

Properties (read-only unless noted): `type`, `length`, `byteLength`, `size`,
`elementLength`, `avail`, `buffer`, `head` *(get/set)*, `oldestTail`
*(get/set)*, `linearInsertRange`.

## `FormParser`

Multipart/urlencoded POST form parser (wraps `lws_spa`).

- `new FormParser(socket, paramNames[, options])` — `socket` is the connection
  `Socket`, `paramNames` an array of expected field names. `options`:
  `onOpen(name, filename)`, `onContent(name, data)`, `onClose(name)`,
  `onFinalize()`, `chunkSize` (default `1024`).

A `FormParser` instance is callable: call it with each received chunk
(`String`/`ArrayBuffer`), and with `null` to finalize parsing. Parsed
parameters are accessible by index on the instance.

Properties: `socket`, `params`, `read` *(read-only)*; `onopen`, `oncontent`,
`onclose`, `onfinalize` *(get/set)*.

## `Hash`

Cryptographic digest / HMAC (wraps lws genhash/genhmac).

- `new Hash(type[, hmacKey])` — `type` is one of `Hash.TYPE_MD5`,
  `Hash.TYPE_SHA1`, `Hash.TYPE_SHA256`, `Hash.TYPE_SHA384`, `Hash.TYPE_SHA512`.
  Passing `hmacKey` (string/ArrayBuffer, SHA-256 or larger types only) creates
  an HMAC.

Methods:

- `update(data)` — feed data (string/ArrayBuffer/TypedArray); returns byte count
- `finalize()` — finish and return the digest as `ArrayBuffer`
- `valueOf()` — digest as `ArrayBuffer` (after finalization)
- `toString([bitsPerChar])` — digest as string (default hex)

Instances are also callable: `hash(data)` updates, `hash()` / `hash(null)`
finalizes. After finalization, digest bytes are readable by index and `length`
gives the digest size.

Properties (read-only): `type`, `byteLength`, `bitLength`, `hmac`, `finalized`.

---

# Constants

Exported at module level:

- `METHOD_GET`, `METHOD_POST`, `METHOD_OPTIONS`, `METHOD_PUT`, `METHOD_PATCH`,
  `METHOD_DELETE`, `METHOD_HEAD` — HTTP method numbers
- `LLL_ERR`, `LLL_WARN`, `LLL_NOTICE`, `LLL_INFO`, `LLL_DEBUG`, `LLL_PARSER`,
  `LLL_HEADER`, `LLL_EXT`, `LLL_CLIENT`, `LLL_LATENCY`, `LLL_USER`,
  `LLL_THREAD`, `LLL_ALL` — log level bits for `setLog()`
- `LWS_WRITE_TEXT`, `LWS_WRITE_BINARY`, `LWS_WRITE_CONTINUATION`,
  `LWS_WRITE_HTTP`, `LWS_WRITE_PING`, `LWS_WRITE_PONG`,
  `LWS_WRITE_HTTP_FINAL`, `LWS_WRITE_HTTP_HEADERS`,
  `LWS_WRITE_HTTP_HEADERS_CONTINUATION`, `LWS_WRITE_BUFLIST`,
  `LWS_WRITE_NO_FIN`, `LWS_WRITE_H2_STREAM_END`,
  `LWS_WRITE_CLIENT_IGNORE_XOR_MASK`, `LWS_WRITE_RAW` — write flags for
  `Socket.prototype.send()`
- `logLevels` — object mapping log level bit values to their names

---

See [example.mjs](./example.mjs) and the [examples](./examples) and
[tests](./tests) directories for more usage examples.
