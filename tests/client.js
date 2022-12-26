import { err, exit, puts } from 'std';
import { setReadHandler, setWriteHandler } from 'os';
import { client, setLog, LLL_WARN, LLL_CLIENT, LLL_USER, URL, Generator, logLevels } from 'net';
import { Levels, DefaultLevels, Init, isDebug, log } from './log.js';
import { escape, abbreviate } from './common.js';

const connections = new Set();

export default function Client(url, options, debug) {
  //console.log('Client',{url,options,debug});
  log('MinnetClient', { url, options });
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

  err.puts(`Client connecting to ${url} ...\n`);

 /* setLog(
    LLL_WARN | LLL_USER,
    (level, message) =>
      !/LOAD_EXTRA|VHOST_CERT_AGING/.test(message) &&
      log(`${logLevels[level].padEnd(10)} ${message.replace(/\n/g, '\\n').trim()}`)
  );*/

  let writable, readable, c, pr, resolve, reject;

  readable = new Generator(async (push, stop) => {
 /*   onMessage = (ws, data) => push(data);
    onClose = onError = (ws, status) => {
      console.log('stop', { ws, status });

      stop(status);
    };
  });
*/
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
    /*  onHttp(req, rsp) {
      const { url, method, headers } = req;

      return onHttp ? onHttp(req, rsp) : (console.log('\x1b[38;5;82monHttp\x1b[0m', { url, method, headers }), rsp);
    },*/
    onFd(fd, rd, wr) {
      setReadHandler(fd, rd);
      setWriteHandler(fd, wr);
    },
    onMessage(ws, msg) {
      /*  if(resolve) resolve({ value: msg, done: false });
      pr = reject = resolve = null;*/

      onMessage
        ? onMessage(ws, msg)
        : (console.log('onMessage', console.config({ maxStringLen: 100 }), { ws, msg }),
          puts(escape(abbreviate(msg)) + '\n'));
    },
    async onHttp(req, resp) {
      // console.log('onHttp', { req, resp });
      let t=  resp.text();

       console.log('onHttp', t);

      t.then(t => {
        console.log('onHttp',t);
        push(t);
      });
      /*for await(let chunk of resp.body) {
        //console.log('onHttp body chunk:', chunk);
        push(chunk);
      }*/
    }
  });
});

  return {
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
