import { exit } from 'std';
import { close, exec, open, realpath, O_RDWR, setReadHandler, setWriteHandler, Worker, kill, SIGUSR1 } from 'os';
import { AsyncIterator, Response, Request, Ringbuffer, Generator, Socket, FormParser, Hash, URL, createServer, client, fetch, getSessions, setLog, METHOD_GET, METHOD_POST, METHOD_OPTIONS, METHOD_PUT, METHOD_PATCH, METHOD_DELETE, METHOD_HEAD, LLL_ERR, LLL_WARN, LLL_NOTICE, LLL_INFO, LLL_DEBUG, LLL_PARSER, LLL_HEADER, LLL_EXT, LLL_CLIENT, LLL_LATENCY, LLL_USER, LLL_THREAD, LLL_ALL, logLevels } from 'net';
import { Levels, isDebug, DebugCallback, DefaultLevels, Init, SetLog, log } from './log.js';
import { codecs, Connection, DecodeValue, DeserializeSymbols, DeserializeValue, EncodeValue, FactoryClient, FactoryEndpoint, GetKeys, GetProperties, MessageReceiver, MessageTransceiver, MessageTransmitter, parseURL, RPC_INTERNAL_ERROR, RPC_INVALID_PARAMS, RPC_INVALID_REQUEST, RPC_METHOD_NOT_FOUND, RPC_PARSE_ERROR, RPC_SERVER_ERROR_BASE, RPCApi, RPCClient, RPCConnect, RPCFactory, RPCListen, RPCObject, RPCProxy, RPCServer, RPCSocket, SerializeValue } from '../js/rpc.js';
import { assert, getpid, once, exists, randStr, escape, abbreviate, save, MakeCert } from './common.js';

const w = Worker.parent;
const name = w ? 'CHILD\t' : 'PARENT\t';

function define(obj, ...args) {
  for(let props of args) {
    const desc = Object.getOwnPropertyDescriptors(props),
      keys = Object.getOwnPropertyNames(props).concat(Object.getOwnPropertySymbols(props));

    for(let prop of keys) {
      try {
        delete obj[prop];
      } catch(e) {}
      Object.defineProperty(obj, prop, desc[prop]);
    }
  }
  return obj;
}

export function MakeSendFunction(sendFn, returnFn) {
  return returnFn ? msg => (sendFn(msg), returnFn()) : sendFn;
}

export function Server(...args) {
  const mydir = args[0].replace(/(\/|^)[^\/]*$/g, '$1.').replace(/\/\.$/g, '');
      log(`My directory '${mydir}'`);

  const parentDir = mydir + '/..';

  if(/(^|\/)server\.js$/.test(args[0])) {
    const sslCert = mydir + '/localhost.crt',
      sslPrivateKey = mydir + '/localhost.key';

    if(!(exists(sslCert) && exists(sslPrivateKey))) {
      log(`Generating certificate '${sslCert}'`);
      MakeCert(sslCert, sslPrivateKey, 'localhost');
    }

    const host = args[1] ?? 'localhost',
      port = args[2] ? +args[2] : 30000;

    log('MinnetServer', { host, port });

    setLog(LLL_WARN | LLL_USER, (level, message) => !/LOAD_EXTRA|VHOST_CERT_AGING|EVENT_WAIT/.test(message) && log(`${logLevels[level].padEnd(10)} ${message.trim()}`));

    const fdmap = (globalThis.fdmap = {});
    const connections = (globalThis.connections = new Map());

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

  return  createServer(
      (globalThis.options = {
        block: false,
        tls: true,
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
          log('onConnect(1)', { ws, req });
          log('onConnect(2)', req.url);

          globalThis.req = req;

          connections.set(ws.fd, ws);

          let o = (fdmap[ws.fd] = {
            server: new RPCServer(undefined, undefined, {
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
            })
          });

          o.generator = new AsyncIterator();
          o.send = MakeSendFunction(
            msg => ws.send(msg),
            () => o.generator.next()
          );
        },
        onClose: (ws, status, reason) => {
          log('onClose', { ws, status, reason });
          ws.close(status);

          connections.delete(ws.fd);
          // if(status >= 1000) exit(status - 1000);
        },
        onError: (ws, error) => {
          log('onError', { ws, error });
        },
        onRequest: (ws, req, rsp) => {
          log('onRequest', { req, rsp });
        },
        /*onFd: (fd, rd, wr) => {
          log('onFd', { fd, rd, wr });
          setReadHandler(fd, rd);
          setWriteHandler(fd, wr);
        },*/
        onMessage: (ws, msg) => {
          let serv, resolve;
          try {
            log('onMessage(1)', msg, fdmap[ws.fd]);
            let o = fdmap[ws.fd];

            if(o && o.generator) {
              let r = o.generator.push(msg);
              log(`o.generator.push(${msg}) =`, r);
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

          ws.send('ECHO: ' + msg);
        },
        onCertificateVerify(obj) {
          log(`\x1b[1;33monCertificateVerify\x1b[0m`, obj );

globalThis.verify=obj;

          obj.ok=2;
         }
      })
    );
  }
}

function main() {
  Init(scriptArgs[0]);

  if(w) {
    try {
      log(`Starting worker`, w);

      MinnetServer.worker(w);
    } catch(error) {
      w.postMessage({ type: 'error', error });
    }
  } else {
    let server;

    try {
      const args = [...(globalThis.scriptArgs ?? process.argv)];

    server=globalThis.server=  Server(...args);
    } catch(error) {
      log('ERROR', error);
    }

       log(`Started server`, server);
       import('util').then(m => m.startInteractive())
 }
}

main();
