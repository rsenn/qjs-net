import { exit } from 'std';
import { close, exec, open, realpath, O_RDWR, setReadHandler, setWriteHandler, Worker } from 'os';
import { server, URL, setLog, LLL_ERR, LLL_WARN, LLL_NOTICE, LLL_INFO, LLL_DEBUG, LLL_PARSER, LLL_HEADER, LLL_EXT, LLL_CLIENT, LLL_LATENCY, LLL_USER, LLL_THREAD } from 'net';
import { Levels, DefaultLevels, Init, isDebug, log } from './log.js';
import { getpid, once, exists } from './common.js';

const w = Worker.parent;
const name = w ? 'CHILD\t' : 'PARENT\t';
//let log = (...args) => console.log(name, ...args);
const connections = new Set();

/*import('console').then(({ Console }) => { globalThis.console = new Console(err, { inspectOptions: { compact: 0, customInspect: true, maxStringLength: 100 } });
  log = (...args) => globalThis.console.log(name, ...args);
});*/

const MimeTypes = [
  ['.svgz', 'application/gzip'],
  ['.mjs', 'application/javascript'],
  ['.wasm', 'application/octet-stream'],
  ['.eot', 'application/vnd.ms-fontobject'],
  ['.lib', 'application/x-archive'],
  ['.bz2', 'application/x-bzip2'],
  ['.gitignore', 'text/plain'],
  ['.cmake', 'text/plain'],
  ['.hex', 'text/plain'],
  ['.md', 'text/plain'],
  ['.pbxproj', 'text/plain'],
  ['.wat', 'text/plain'],
  ['.c', 'text/x-c'],
  ['.h', 'text/x-c'],
  ['.cpp', 'text/x-c++'],
  ['.hpp', 'text/x-c++'],
  ['.filters', 'text/xml'],
  ['.plist', 'text/xml'],
  ['.storyboard', 'text/xml'],
  ['.vcxproj', 'text/xml'],
  ['.bat', 'text/x-msdos-batch'],
  ['.mm', 'text/x-objective-c'],
  ['.m', 'text/x-objective-c'],
  ['.sh', 'text/x-shellscript']
];

export function MakeCert(sslCert, sslPrivateKey) {
  let stderr = open('/dev/null', O_RDWR);
  let ret = exec(
    [
      'openssl',
      'req',
      '-x509',
      '-out',
      sslCert,
      '-keyout',
      sslPrivateKey,
      '-newkey',
      'rsa:2048',
      '-nodes',
      '-sha256',
      '-subj',
      '/CN=localhost'
    ],
    { stderr }
  );
  close(stderr);
  return ret;
}

export class MinnetServer {
  static ws2id = new WeakMap();

  constructor(options = {}) {
    options.host ??= 'localhost';
    options.port ??= 30000;
    options.tls ??= false;
    options.protocol ??= options.tls ? 'wss' : 'ws';
    options.path ??= '/ws';

    const { sslCert, sslPrivateKey } = options;

    if(sslCert && sslPrivateKey) {
      if(!exists(sslCert) || !exists(sslPrivateKey)) MakeCert(sslCert, sslPrivateKey);
    }
    let { mounts, mimetypes, ...opts } = options;

    for(let prop in mounts) {
      if(typeof mounts[prop] == 'string') {
        let fn;
        eval('fn=' + mounts[prop]);
        //log('fn',fn, mounts[prop]);
        mounts[prop] = fn;
      }
    }

    //    log('mounts',mounts);
    log('MinnetServer.constructor', { mounts, mimetypes, ...opts }); // Object.entries(options).reduce((acc, [n, v]) => (acc ? acc + ', ' : '') + n + '=' + v, ''));

    Object.defineProperty(this, 'options', { get: () => options });
  }

  static #currentId = 0;

  static #newId() {
    let id = (this.#currentId | 0) + 1;
    return (this.#currentId = id);
  }

  static connections() {
    let { ws2id } = MinnetServer;
    return Object.fromEntries(
      [...connections].reduce((acc, wr) => {
        let ws = wr; /*.deref()*/
        if(ws !== undefined) acc.push([ws2id.get(ws), ws]);
        return acc;
      }, [])
    );
  }

  id(ws) {
    let { ws2id } = MinnetServer;
    let id = ws2id.get(ws);
    if(typeof id != 'number') {
      id = MinnetServer.#newId();
      ws2id.set(ws, id);
    }
    return id;
  }

  start() {
    const { id, host, port } = this;

    let started = once(() => w.postMessage({ type: 'running' }));

    Init('server.js');

    let { mounts = {}, mimetypes = [], ...options } = this.options;

    mounts = {
      proxy(req, res) {
        const { url, method, headers } = req;
        const { status, ok, type } = res;

        log('proxy', { url, method, headers }, { status, ok, url, type });
      },
      *config(req, res) {
        log('/config', { req, res });
        yield '{}';
      },
      ...mounts,
      '/': ['/', '.', 'index.html'],
      '/404.html': function* (req, res) {
        log('/404.html', { req, res });
        yield '<html><head><meta charset=utf-8 http-equiv="Content-Language" content="en"/><link rel="stylesheet" type="text/css" href="/error.css"/></head><body><h1>403</h1></body></html>';
      }
    };

    log('mounts', mounts);

    mimetypes = { ...MimeTypes, ...mimetypes };

    server({
      mimetypes,
      mounts,
      onConnect: (ws, req) => {
        log('onConnect', { ws, req });
        const { url, path } = req;
        const { family, address, port } = ws;

        w.postMessage({
          type: 'connect',
          id: this.id(ws),
          url,
          path,
          family,
          address,
          port
        });
        connections.add(ws);
      },
      onClose: (ws, status) => {
        connections.delete(ws);
        w.postMessage({ type: 'close', id: this.id(ws), status });
      },
      onError: (ws, error) => {
        w.postMessage({ type: 'error', id: this.id(ws), error });
      },
      onHttp: (req, rsp) => {
        log('onHttp', { req, rsp });
        const { url, path } = req;
        w.postMessage({ type: 'http', id: this.id(ws), url, path });
      },
      onFd: (fd, rd, wr) => {
        log('onFd', { fd, rd, wr });
        started();
        setReadHandler(fd, rd);
        setWriteHandler(fd, wr);
      },
      onMessage: (ws, msg) => {
        log('onMessage', { ws, msg });
        w.postMessage({ type: 'message', id: this.id(ws), msg });
      },
      ...options
    });
    w.postMessage({ type: 'exit' });
  }

  run() {
    let counter = 0;

    this.worker = new Worker('./server.js');
    const { worker, options } = this;
    const { host, port, sslCert, sslPrivateKey } = this.options;

    let ret = {
      sendMessage: msg => worker.postMessage(msg)
    };

    let opts = Object.entries(options).map(([k, v]) => {
      if(typeof v == 'object')
        for(let prop in v)
          if(typeof v[prop] == 'function') {
            v[prop] = v[prop] + '';
            if(!v[prop].startsWith('function')) v[prop] = 'function ' + v[prop];
          }

      return [k, v];
    });
    log('MinnetServer.run', opts);

    worker.onmessage = e => {
      let ev = e.data;
      //log('worker.onmessage', ev);
      switch (ev.type) {
        case 'ready':
          worker.postMessage({
            type: 'start',
            ...opts.reduce((acc, [k, v]) => ({ ...acc, [k]: v }), {})
          });
          break;

        default:
          if(typeof ret.onmessage == 'function') ret.onmessage(ev);
          else log(`Worker.run`, ev);

          break;
      }
    };

    return ret;
  }

  static worker(w) {
    let i;
    let server;

    w.onmessage = function(e) {
      let ev = e.data;
      const { type, ...event } = ev;
      switch (type) {
        case 'start':
          const { host, port } = event;
          log(`Starting server on ${host}:${port}...`);
          server = globalThis.server = new MinnetServer(event);
          server.start();
          break;
        case 'exit':
          const { exitcode } = event;
          exit(exitcode ?? 0);
          break;

        case 'send':
          const { id, msg } = event;
          let ws = MinnetServer.connections()[id];
          ws.send(msg);
          break;

        default:
          log(`No such message type '${type}'`);
          return;
      }
      //log(`worker.onmessage`, JSON.stringify(e));
    };

    w.postMessage({ type: 'ready' });
  }
}

if(w) {
  try {
    log(`Starting worker`, w);

    MinnetServer.worker(w);
  } catch(error) {
    w.postMessage({ type: 'error', error });
  }
} else {
  try {
    const args = [...(globalThis.scriptArgs ?? process.argv)];
    const mydir = args[0].replace(/(\/|^)[^\/]*$/g, '$1.');

    const parentDir = mydir + '/..';

    if(/(^|\/)server\.js$/.test(args[0])) {
      const sslCert = mydir + '/localhost.crt',
        sslPrivateKey = mydir + '/localhost.key';

      const host = args[1] ?? 'localhost',
        port = args[2] ? +args[2] : 30000;

      log('MinnetServer', { host, port });

      Init('server.js');

      server({
        tls: true,
        mimetypes: MimeTypes,
        host,
        port,
        protocol: 'http',
        sslCert,
        sslPrivateKey,
        mounts: {
          '/': ['/', parentDir, 'index.html'],
          '/404.html': function* (req, res) {
            log('/404.html', { req, res });
            yield '<html><head><meta charset=utf-8 http-equiv="Content-Language" content="en"/><link rel="stylesheet" type="text/css" href="/error.css"/></head><body><h1>403</h1></body></html>';
          },
          *generator(req, res) {
            log('/generator', { req, res });
            yield 'This';
            yield ' ';
            yield 'is';
            yield ' ';
            yield 'a';
            yield ' ';
            yield 'generated';
            yield ' ';
            yield 'response';
            yield '\n';
          }
        },
        onConnect: (ws, req) => {
          /*const { url, path } = req;
          const { family, address, port } = ws;
          log('onConnect', { url, path, family, address, port });*/
          log('onConnect', { ws, req });
        },
        onClose: (ws, status, reason) => {
          log('onClose', { ws, status, reason });
          ws.close(status);
          // if(status >= 1000) exit(status - 1000);
        },
        onError: (ws, error) => {
          log('onError', { ws, error });
        },
        onHttp: (req, rsp) => {
          log('onHttp', { req, rsp });
        },
        onFd: (fd, rd, wr) => {
          //log('onFd', { fd, rd, wr });
          setReadHandler(fd, rd);
          setWriteHandler(fd, wr);
        },
        onMessage: (ws, msg) => {
          log('onMessage', typeof ws, { ws, msg });
          ws.send('ECHO: ' + msg);
          //ws.send(JSON.stringify({ type: 'message', msg }));
        }
      });
    }
  } catch(error) {
    log('ERROR', error);
  }
}
