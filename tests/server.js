import { exit } from 'std';
import { close, exec, open, realpath, O_RDWR, setReadHandler, setWriteHandler, Worker, kill, SIGUSR1 } from 'os';
import { AsyncIterator, Response, Request, Ringbuffer, Generator, Socket, FormParser, Hash, URL, createServer, client, fetch, getSessions, setLog, METHOD_GET, METHOD_POST, METHOD_OPTIONS, METHOD_PUT, METHOD_PATCH, METHOD_DELETE, METHOD_HEAD, LLL_ERR, LLL_WARN, LLL_NOTICE, LLL_INFO, LLL_DEBUG, LLL_PARSER, LLL_HEADER, LLL_EXT, LLL_CLIENT, LLL_LATENCY, LLL_USER, LLL_THREAD, LLL_ALL, logLevels } from 'net.so';
import { Levels, DefaultLevels, Init, isDebug } from './log.js';
import { getpid, once, exists } from './common.js';
import { SerializeValue, MessageReceiver, MessageTransmitter, MessageTransceiver, codecs, RPCApi, RPCProxy, RPCObject, RPCFactory, Connection, RPC_PARSE_ERROR, RPC_INVALID_REQUEST, RPC_METHOD_NOT_FOUND, RPC_INVALID_PARAMS, RPC_INTERNAL_ERROR, RPC_SERVER_ERROR_BASE, FactoryEndpoint, RPCServer, RPCClient, FactoryClient, RPCSocket, GetProperties, GetKeys, DeserializeSymbols, DeserializeValue, RPCConnect, RPCListen } from '../js/rpc.js';

function define(obj, ...args) {
  const propdesc = {};

  for(let props of args) {
    let desc = Object.getOwnPropertyDescriptors(props);

    for(let prop of GetKeys(desc)) {
      propdesc[prop] = { ...desc[prop], enumerable: false, configurable: true };

      if('value' in propdesc[prop]) propdesc[prop].writable = true;
    }
  }

  Object.defineProperties(obj, propdesc);
  return obj;
}

export function MakeSendFunction(sendFn, returnFn) {
  return returnFn ? msg => (sendFn(msg), returnFn()) : sendFn;
}

let log;

const w = Worker.parent;
const name = w ? 'CHILD\t' : 'PARENT\t';
const connections = new Set();

const classes = {
  AsyncIterator,
  Response,
  Request,
  Ringbuffer,
  Generator,
  Socket,
  Hash,
  URL,
  Array,
  Map,
  Set
};

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
  let ret = exec(['openssl', 'req', '-x509', '-out', sslCert, '-keyout', sslPrivateKey, '-newkey', 'rsa:2048', '-nodes', '-sha256', '-subj', '/CN=localhost'], { stderr });
  close(stderr);
  return ret;
}

function main() {
  /*  log=console.log;

import('console').then(({ Console }) => { globalThis.console = new Console(err, { inspectOptions: { compact: 0, customInspect: true, maxStringLength: 100 } });
  log = (...args) => globalThis.console.log(name, ...args);
});
*/

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
        Init('server.js');

        console.log('MinnetServer', { host, port });

        setLog(LLL_WARN | LLL_USER, (level, message) => !/LOAD_EXTRA|VHOST_CERT_AGING|EVENT_WAIT/.test(message) && log(`${logLevels[level].padEnd(10)} ${message.trim()}`));
        //  setLog(0, (level, message) => {});
        //
        let fdmap = (globalThis.fdmap = {});
        let connections = new Map();

        define(
          globalThis,
          {
            get connections() {
              return [...connections].map(([fd, ws]) => ws);
            },
            fd2ws: n => connections.get(n)
          },
          {
            RPCApi,
            RPCClient,
            RPCConnect,
            RPCFactory,
            RPCListen,
            RPCObject,
            RPCProxy,
            RPCServer,
            RPCSocket,
            SerializeValue
          }
        );

        createServer(
          (globalThis.options = {
            block: false,
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
              console.log('onConnect(1)', { ws, req });
              console.log('onConnect(2)', req.url);

              globalThis.req = req;

              connections.set(ws.fd, ws);

              let o = (fdmap[ws.fd] = { server: new RPCServer(undefined, undefined, classes) });

              o.generator = new AsyncIterator();
              o.send = MakeSendFunction(
                msg => ws.send(msg),
                () => o.generator.next()
              );
            },
            onClose: (ws, status, reason) => {
              console.log('onClose', { ws, status, reason });
              ws.close(status);

              connections.delete(ws.fd);
              // if(status >= 1000) exit(status - 1000);
            },
            onError: (ws, error) => {
              console.log('onError', { ws, error });
            },
            onRequest: (ws, req, rsp) => {
              console.log('onRequest', { req, rsp });
            },
            onFd: (fd, rd, wr) => {
              console.log('onFd', { fd, rd, wr });
              setReadHandler(fd, rd);
              setWriteHandler(fd, wr);
            },
            onMessage: (ws, msg) => {
              let serv, resolve;
              try {
                console.log('onMessage(1)', msg, fdmap[ws.fd]);
                let o = fdmap[ws.fd];

                if(o && o.generator) {
                  let r = o.generator.push(msg);
                  console.log(`o.generator.push(${msg}) =`, r);
                  if(r) return;
                }
              } catch(e) {}

              if((serv = fdmap[ws.fd].server)) {
                let response;
                try {
                  if((response = Connection.prototype.onmessage.call(serv, msg))) {
                    ws.send(JSON.stringify(response));
                    return;
                  }
                } catch(e) {}
              }
              //console.log('onMessage(4)', { ws, msg });
              ws.send('ECHO: ' + msg);
              //ws.send(JSON.stringify({ type: 'message', msg }));
            }
          })
        );
      }
    } catch(error) {
      console.log('ERROR', error);
    }
  }
}

main();
