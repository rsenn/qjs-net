import * as std from 'std';
import * as os from 'os';
import net, {
  URL,
  LLL_ERR,
  LLL_WARN,
  LLL_NOTICE,
  LLL_INFO,
  LLL_DEBUG,
  LLL_PARSER,
  LLL_HEADER,
  LLL_EXT,
  LLL_CLIENT,
  LLL_LATENCY,
  LLL_USER,
  LLL_THREAD
} from 'net';

const w = os.Worker.parent;
const name = w ? 'CHILD\t' : 'PARENT\t';
const getpid = () => parseInt(os.readlink('/proc/self')[0]);
const log = /*w ? (...args) => print(name, ...args) :*/ (...args) => console.log(name, ...args);
const connections = new  Set();
const logLevels = Object.getOwnPropertyNames(net)
  .filter(n => /^LLL_/.test(n))
  .reduce((acc, n) => {
    let v = Math.log2(net[n]);
    if(Math.floor(v) === v) acc[net[n]] = n.substring(4);
    return acc;
  }, {});

log('logLevels', JSON.stringify(logLevels));

const once = fn => {
  let ret,
    ran = false;
  return (...args) => (ran ? ret : ((ran = true), (ret = fn.apply(this, args))));
};

export class MinnetServer {
  static ws2id = new WeakMap();

  constructor(options = {}) {
    options.host ??= 'localhost';
    options.port ??= 30000;
    options.tls ??= false;
    options.protocol ??= options.tls ? 'wss' : 'ws';
    options.path ??= '/ws';

    log(
      'MinnetServer.constructor',
      Object.entries(options).reduce((acc, [n, v]) => (acc ? acc + ', ' : '') + n + '=' + v, '')
    );

    Object.defineProperty(this, 'options', { get: () => options });
  }

  static #currentId = 0;

  static #newId() {
    let id = (this.#currentId | 0) + 1;
    return (this.#currentId = id);
  }

  static connections() {
       let { ws2id } = MinnetServer;
 return [...connections].reduce((acc,wr) => {
      let ws = wr.deref();
      if(ws!==undefined)
        acc.push([ws2id.get(ws), ws]);
      return acc;
    }, []);
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

    net.setLog(
      LLL_ERR |
        LLL_WARN |
        /*LLL_NOTICE | LLL_INFO | LLL_DEBUG |LLL_PARSER | 
        LLL_HEADER |
        LLL_EXT |*/
        LLL_CLIENT |
        LLL_LATENCY |
        LLL_USER |
        LLL_THREAD,
      (level, msg) => {
        const l = logLevels[level];
        const n = Math.log2(level);

        if(level >= LLL_NOTICE && level <= LLL_EXT) return;

        if(l == 'USER') print(`SERVER   ${msg}`);
        else log(`${l.padEnd(10)} ${msg}`);
      }
    );

    net.server({
      ...this.options,
      mimetypes: [
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
      ],
      mounts: {
        '/': ['/', '.', 'index.html'],
        '/404.html': function* (req, res) {
          console.log('/404.html', { req, res });
          yield '<html><head><meta charset=utf-8 http-equiv="Content-Language" content="en"/><link rel="stylesheet" type="text/css" href="/error.css"/></head><body><h1>403</h1></body></html>';
        }
      },
      onConnect: (ws, req) => {
        const { url, path } = req;

        w.postMessage({ type: 'connect', id: this.id(ws), url, path });
        // log('onConnect', +ws, req);
        connections.add(new WeakRef(ws));
      },
      onClose: (ws, status) => {
        connections.delete(ws);
        w.postMessage({ type: 'close', id: this.id(ws), status });
      },
      onError: (ws, error) => {
        w.postMessage({ type: 'error', id: this.id(ws), error });
      },
      onHttp: (req, rsp) => {
        const { url, path } = req;
        w.postMessage({ type: 'http', id: this.id(ws), url, path });
      },
      onFd: (fd, rd, wr) => {
        started();
        os.setReadHandler(fd, rd);
        os.setWriteHandler(fd, wr);
      },
      onMessage: (ws, msg) => {
        log('onMessage', { ws, msg });
        w.postMessage({ type: 'message', id: this.id(ws), msg });
        //std.puts(escape(abbreviate(msg)) + '\n');
      }
    });
    w.postMessage({ type: 'exit' });
  }

  run() {
    let counter = 0;

    this.worker = new os.Worker('./server.js');
    const { worker } = this;
    const { host, port, sslCert, sslPrivateKey } = this.options;
    log('MinnetServer.run', { worker, host, port });

let ret={};

    worker.onmessage = function(e) {
      let ev = e.data;
          log('worker.onmessage', ev);
  switch (ev.type) {
        case 'ready':
          worker.postMessage({ type: 'start', host, port, sslCert, sslPrivateKey });
          break;

        default:
          if(typeof ret.onmessage == 'function') ret.onmessage(ev);
          else
                      log(`Worker.run`, ev);

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

        case 'send':
        const {id, msg}= event;
        let c=MinnetServer.connections();
                  log(`send`,{id,msg,c});

        break;
        default:
          log(`No such message type '${type}'`);
          return;
      }
      log(`worker.onmessage`, JSON.stringify(e));
    };

    w.postMessage({ type: 'ready' });
  }
}

log(`worker: ${w}`);

if(w) {
 log(`Starting worker`, w);

  import('console').then(
    ({ Console }) =>
      (globalThis.console = new Console({ inspectOptions: { compact: 2, customInspect: true, maxStringLength: 100 } }))
  );

  try {
    MinnetServer.worker(w);
  } catch(error) {
    w.postMessage({ type: 'error', error });
  }
}
