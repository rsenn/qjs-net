import { err, exit, puts } from 'std';
import { setReadHandler, setWriteHandler } from 'os';
import { client, setLog, LLL_WARN, LLL_CLIENT, LLL_USER, URL, Generator, logLevels } from 'net.so';
import { Levels, DefaultLevels, Init, isDebug, log } from './log.js';
import { escape, abbreviate } from './common.js';

const connections = new Set();

export default function Client(url, options, debug) {
  //log('MinnetClient', { url, options });
  Init('client.js', typeof debug == 'number' ? debug : LLL_CLIENT | (debug ? LLL_USER : 0));

  let {
    onConnect,
    onClose,
    onError,
    onHttp,
    onFd,
    onMessage,
    tls = true,
    sslCert = 'localhost.crt',
    sslPrivateKey = 'localhost.key',
    headers = {},
    ...opts
  } = options;

  log(`Client connecting to ${url} ...`);

  /* setLog(
    LLL_WARN | (debug ? LLL_USER : 0),
    (level, message) =>
      !/LOAD_EXTRA|VHOST_CERT_AGING/.test(message) &&
      log(`${logLevels[level].padEnd(10)} ${message.replace(/\n/g, '\\n').trim()}`)
  );
*/
  let writable,
    readable,
    c,
    pr,
    resolve,
    reject,
    remoteName = new Promise((resolve, reject) => {
      readable = new Generator(async (push, stop) => {
        c = client(url, {
          tls,
          sslCert,
          sslPrivateKey,
          headers: {
            'User-Agent': 'minnet',
            ...headers
          },
          ...opts,
          onConnect(ws, req) {
            console.log('onConnect');
            connections.add(ws);

            writable = {
              write(chunk) {
                return ws.send(chunk);
              }
            };

            onConnect ? onConnect(ws, req) : console.log('onConnect', ws, req);
          },
          onClose(ws, status, reason, error) {
            connections.delete(ws);

            if(resolve) resolve({ value: { status, reason, error }, done: true });

            onClose
              ? onClose(ws, status, reason, error)
              : (console.log('onClose', { ws, reason }), exit(reason != 1000 && reason != 0 ? 1 : 0));
            pr = reject = resolve = null;
          },
          onError(ws, error) {
            connections.delete(ws);

            onError ? onError(ws, error) : (console.log('onError', { ws, error }), exit(error));
          },

          onFd(fd, rd, wr) {
            setReadHandler(fd, rd);
            setWriteHandler(fd, wr);
          },
          onMessage(ws, msg) {
            onMessage
              ? onMessage(ws, msg)
              : (console.log('onMessage', console.config({ maxStringLen: 100 }), { ws, msg }),
                puts(escape(abbreviate(msg)) + '\n'));
          },
          async onHttp(req, resp) {
            let text = await resp.text();
            let { path } = resp.url;

            path = path.replace(/.*\//g, '');
            resolve(path || 'index.html');

            log('onHttp(1)', resp.url.path);
            log('onHttp(2)', text.replace(/\n/g, '\\n').substring(0, 100));

            console.log('push', /* await*/ push(text));
          }
        });
      });
    });

  return {
    get remoteName() {
      return remoteName;
    },
    readable: Object.defineProperties(
      {},
      {
        [Symbol.asyncIterator]: {
          value: () => readable
        },
        getReader: {
          value: () => ({ read: () => readable.next() })
        },
        [Symbol.toStringTag]: { value: 'ReadableStream' }
      }
    ),
    writable
  };
}

Object.defineProperty(Client, 'connections', {
  get() {
    return [...connections];
  }
});
