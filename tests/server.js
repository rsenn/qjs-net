import { exit } from 'std';
import { close, exec, open, realpath, O_RDWR, setReadHandler, setWriteHandler, Worker } from 'os';
import { server, URL, setLog, logLevels, LLL_ALL, LLL_ERR, LLL_WARN, LLL_NOTICE, LLL_INFO, LLL_DEBUG, LLL_PARSER, LLL_HEADER, LLL_EXT, LLL_CLIENT, LLL_LATENCY, LLL_USER, LLL_THREAD } from '../net.so';
import { Levels, DefaultLevels, Init, isDebug, log } from './log.js';
import { getpid, once, exists } from './common.js';

const w = Worker.parent;
const name = w ? 'CHILD\t' : 'PARENT\t';
//let log = (...args) => console.log(name, ...args);
let log = (...args) => console.log(...args);
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

      setLog(
        LLL_WARN | LLL_USER,
        (level, message) =>
          !/LOAD_EXTRA|VHOST_CERT_AGING|EVENT_WAIT/.test(message) &&
          log(`${logLevels[level].padEnd(10)} ${message.trim()}`)
      );
      //  setLog(0, (level, message) => {});

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
        onHttp: (ws, req, rsp) => {
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
