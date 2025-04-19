import { client, Generator, LLL_CLIENT, LLL_USER, LLL_WARN, LLL_INFO, LLL_ALL, logLevels, setLog } from 'net.so';
import { setReadHandler, setWriteHandler } from 'os';
import { abbreviate, escape } from './common.js';
import { Init, Levels, log } from './log.js';
import { err, exit, puts } from 'std';

const connections = new Set();

export default function Client(url, options, debug) {
  //log('MinnetClient', { url, options });
  Init('client.js', typeof debug == 'number' ? debug : LLL_CLIENT | (debug ? LLL_USER : 0) | LLL_INFO | LLL_ALL);

  let { onConnect, onClose, onError, onRequest, onFd, onMessage, tls = true, sslCert = 'localhost.crt', sslPrivateKey = 'localhost.key', headers = {}, ...opts } = options;

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
    pr,
    resolve,
    reject,
    remoteName = new Promise((resolve, reject) => {
      readable = new Generator(async (push, stop) =>
        client(url, {
          tls,
          sslCert,
          sslPrivateKey,
          block: false,
          headers: {
            'User-Agent': 'minnet',
            ...headers,
          },
          ...opts,
          onConnect(ws, req) {
            log('onConnect');
            connections.add(ws);

            writable = {
              write(chunk) {
                return ws.send(chunk);
              },
            };

            onConnect ? onConnect(ws, req) : log('onConnect', ws, req);
          },
          onClose(ws, status, reason, error) {
            connections.delete(ws);

            if(resolve) resolve({ value: { status, reason, error }, done: true });
            else stop({ status, reason, error });

            onClose ? onClose(ws, status, reason, error) : (log('onClose', { ws, reason }), exit(reason != 1000 && reason != 0 ? 1 : 0));
            pr = reject = resolve = null;
          },
          onError(ws, error) {
            connections.delete(ws);

            onError ? onError(ws, error) : (log('onError', { ws, error }), exit(error));
          },

          onFd(fd, rd, wr) {
            console.log('onFd', { fd, rd, wr });
            setReadHandler(fd, rd);
            setWriteHandler(fd, wr);
          },
          onMessage(ws, msg) {
            onMessage ? onMessage(ws, msg) : (log('onMessage', config({ maxStringLen: 100 }), { ws, msg }), puts(escape(abbreviate(msg)) + '\n'));
          },
          async onRequest(req, resp) {
            let text = await resp.text();
            let { path } = resp.url;

            path = path.replace(/.*\//g, '');
            resolve(path || 'index.html');

            log('onRequest(1)', resp.url.path);
            log('onRequest(2)', text.replace(/\n/g, '\\n').substring(0, 100));

            log('push', /* await*/ push(text));
          },
        }),
      );
    });

  return {
    get remoteName() {
      return remoteName;
    },
    readable: Object.defineProperties(
      {},
      {
        [Symbol.asyncIterator]: {
          value: () => readable,
        },
        getReader: {
          value: () => ({ read: () => readable.next() }),
        },
        [Symbol.toStringTag]: { value: 'ReadableStream' },
      },
    ),
    writable,
  };
}

Object.defineProperty(Client, 'connections', {
  get() {
    return [...connections];
  },
});
