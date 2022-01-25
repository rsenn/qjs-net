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
import * as std from 'std';
import * as os from 'os';

const parent = os.Worker.parent;
const name = parent ? 'CHILD\t' : 'PARENT\t';
const getpid = () => parseInt(os.readlink('/proc/self')[0]);
const log = parent ? (...args) => print(name, ...args) : (...args) => console.log(name, ...args);
const connections = new Set();
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

  start() {
    const { host, port } = this;

    let started = once(() => parent.postMessage({ type: 'running' }));

    net.setLog(
      LLL_ERR |
        LLL_WARN |
        /*LLL_NOTICE | LLL_INFO | LLL_DEBUG |LLL_PARSER | */ LLL_HEADER |
        LLL_EXT |
        LLL_CLIENT |
        LLL_LATENCY |
        LLL_USER |
        LLL_THREAD,
      (level, msg) => {
        const l = logLevels[level];

        if(['DEBUG', 'INFO'].indexOf(l) == -1) log(`${l.padEnd(10)} ${msg}`);
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
      onConnect(ws, req) {
        parent.postMessage({ type: 'connect', fd: +ws, url: req.url });
        log('onConnect', +ws, req.url);
        connections.add(ws);
        /*    try {
          log(`Connected to ${protocol}://${host}:${port}${path}`, true);
        } catch(err) {
          log('error:', err.message);
        }*/
      },
      onClose(ws, status) {
        connections.delete(ws);
        log('onClose', { ws, status });
        std.exit(status != 1000 ? 1 : 0);
      },
      onError(ws, error) {
        log('onError', { ws, error });
        std.exit(error);
      },
      onHttp(req, rsp) {
        const { url, method, headers } = req;
        return rsp;
      },
      onFd(fd, rd, wr) {
        started();
        log('onFd', JSON.stringify({ fd, rd, wr }));
        os.setReadHandler(fd, rd);
        os.setWriteHandler(fd, wr);
      },
      onMessage(ws, msg) {
        log('onMessage', console.config({ maxStringLen: 100 }), { ws, msg });

        std.puts(escape(abbreviate(msg)) + '\n');
      },
      onError(ws, error) {
        log('onError', ws, error);
      }
    });
    parent.postMessage({ type: 'exit' });
  }

  run() {
    let counter = 0;

    this.worker = new os.Worker('./server.js');
    const { worker } = this;
    const { host, port, sslCert, sslPrivateKey } = this.options;
    log('MinnetServer.run', { worker, host, port });

    worker.onmessage = function(e) {
      let ev = e.data;
      switch (ev.type) {
        case 'ready':
          worker.postMessage({ type: 'start', host, port, sslCert, sslPrivateKey });
          break;

        default:
          log(`Worker.run`, ev);
          return;
      }
      log('worker.onmessage', ev);
    };

    return worker;
  }

  static worker(parent) {
    let i;
    let server;

    parent.onmessage = function(e) {
      let ev = e.data;
      const { type, ...event } = ev;
      switch (type) {
        case 'start':
          const { host, port } = event;
          log(`Starting server on ${host}:${port}...`);
          server = globalThis.server = new MinnetServer(event);
          server.start();
          break;
        default:
          log(`No such message type '${type}'`);
          return;
      }
      log(`worker.onmessage`, JSON.stringify(e));
    };

    parent.postMessage({ type: 'ready' });
  }
}

log(`parent: ${parent}`);

if(parent) {
  //log(`Starting worker (${getpid()})`);
  try {
    MinnetServer.worker(parent);
  } catch(error) {
    parent.postMessage({ type: 'error', error });
  }
}
