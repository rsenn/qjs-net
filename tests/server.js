import * as std from 'std';
import * as os from 'os';
import net, { URL, LLL_ERR, LLL_WARN, LLL_NOTICE, LLL_INFO, LLL_DEBUG, LLL_PARSER, LLL_HEADER, LLL_EXT, LLL_CLIENT, LLL_LATENCY, LLL_USER, LLL_THREAD } from 'net';

const w = os.Worker.parent;
const name = w ? 'CHILD\t' : 'PARENT\t';
const getpid = () => parseInt(os.readlink('/proc/self')[0]);
const log = (...args) => console.log(name, ...args);
const connections = new Set();
const logLevels = Object.getOwnPropertyNames(net)
  .filter(n => /^LLL_/.test(n))
  .reduce((acc, n) => {
    let v = Math.log2(net[n]);
    if(Math.floor(v) === v) acc[net[n]] = n.substring(4);
    return acc;
  }, {});

const once = fn => {
  let ret,
    ran = false;
  return (...args) => (ran ? ret : ((ran = true), (ret = fn.apply(this, args))));
};
const exists = path => {
  let [st, err] = os.stat(path);
  return !err;
};

export function MakeCert(sslCert, sslPrivateKey) {
  let stderr = os.open('/dev/null', os.O_RDWR);
  let ret = os.exec(['openssl', 'req', '-x509', '-out', sslCert, '-keyout', sslPrivateKey, '-newkey', 'rsa:2048', '-nodes', '-sha256', '-subj', '/CN=localhost'], { stderr });
  os.close(stderr);
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

    net.setLog(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO | LLL_CLIENT | LLL_LATENCY | LLL_USER | LLL_THREAD, (level, msg) => {
      const l = logLevels[level];
      const n = Math.log2(level);

      if(level >= LLL_NOTICE && level <= LLL_EXT) return;

      if(l == 'USER') print(`SERVER   ${msg}`);
      else log(`${l.padEnd(10)} ${msg}`);
    });
    let { mounts = {}, mimetypes = [], ...options } = this.options;

    mounts = {
      proxy(req, res) {
        const { url, method, headers } = req;
        const { status, ok, type } = res;

        console.log('proxy', { url, method, headers }, { status, ok, url, type });
      },
      *config(req, res) {
        console.log('/config', { req, res });
        yield '{}';
      },
      ...mounts,
      '/': ['/', '.', 'index.html'],
      '/404.html': function* (req, res) {
        console.log('/404.html', { req, res });
        yield '<html><head><meta charset=utf-8 http-equiv="Content-Language" content="en"/><link rel="stylesheet" type="text/css" href="/error.css"/></head><body><h1>403</h1></body></html>';
      }
    };

    log('mounts', mounts);

    net.server({
      mimetypes: [['.svgz', 'application/gzip'], ['.mjs', 'application/javascript'], ['.wasm', 'application/octet-stream'], ['.eot', 'application/vnd.ms-fontobject'], ['.lib', 'application/x-archive'], ['.bz2', 'application/x-bzip2'], ['.gitignore', 'text/plain'], ['.cmake', 'text/plain'], ['.hex', 'text/plain'], ['.md', 'text/plain'], ['.pbxproj', 'text/plain'], ['.wat', 'text/plain'], ['.c', 'text/x-c'], ['.h', 'text/x-c'], ['.cpp', 'text/x-c++'], ['.hpp', 'text/x-c++'], ['.filters', 'text/xml'], ['.plist', 'text/xml'], ['.storyboard', 'text/xml'], ['.vcxproj', 'text/xml'], ['.bat', 'text/x-msdos-batch'], ['.mm', 'text/x-objective-c'], ['.m', 'text/x-objective-c'], ['.sh', 'text/x-shellscript'], ...mimetypes],
      mounts,
      onConnect: (ws, req) => {
        const { url, path } = req;
        const { family, address, port } = ws;

        w.postMessage({ type: 'connect', id: this.id(ws), url, path, family, address, port });
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
        started();
        os.setReadHandler(fd, rd);
        os.setWriteHandler(fd, wr);
      },
      onMessage: (ws, msg) => {
        //log('onMessage', { ws, msg });
        w.postMessage({ type: 'message', id: this.id(ws), msg });
      },
      ...options
    });
    w.postMessage({ type: 'exit' });
  }

  run() {
    let counter = 0;

    this.worker = new os.Worker('./server.js');
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
          worker.postMessage({ type: 'start', ...opts.reduce((acc, [k, v]) => ({ ...acc, [k]: v }), {}) });
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
          std.exit(exitcode ?? 0);
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
  import('console').then(({ Console }) => (globalThis.console = new Console({ inspectOptions: { compact: 2, customInspect: true, maxStringLength: 100 } })));

  try {
    log(`Starting worker`, w);

    MinnetServer.worker(w);
  } catch(error) {
    w.postMessage({ type: 'error', error });
  }
}
