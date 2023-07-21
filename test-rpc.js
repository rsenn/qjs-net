import { client, createServer, LLL_NOTICE, LLL_USER, LLL_WARN, setLog, URL } from 'net';
import * as os from 'os';
import { define, filter, getOpt, showHelp, split, toUnixTime } from 'util';
import { Console } from '../qjs-modules/lib/console.js';
import inspect from 'inspect';
import { REPL } from '../qjs-modules/lib/repl.js';
import * as std from 'std';
import * as io from 'io';
import { MessageReceiver, MessageTransmitter, MessageTransceiver, codecs, RPCApi, RPCProxy, RPCObject, FactoryClient, RPCFactory, FactoryEndpoint, RPCServer, RPCClient, RPCSocket, RPCConnect, RPCListen } from './js/rpc.js';

import { Directory } from 'directory';
import { List } from 'list';
import { Lexer } from 'lexer';
import { Location } from 'location';
import { SockAddr, Socket, AsyncSocket } from 'sockets';
import { StreamReader, StreamWriter, ReadableStream, ReadableStreamDefaultController, WritableStream, WritableStreamDefaultController, TransformStream } from 'stream';

function ReadJSON(filename) {
  let data = std.loadFile(filename);
  if(data) console.debug(`${data.length} bytes read from '${filename}'`);
  return data ? JSON.parse(data) : null;
}

function WriteFile(name, data, verbose = true) {
  const f = std.open(name, 'w+');
  typeof data == 'string' ? f.puts(data) : f.write(data, 0, data.byteLength);
  let ret = f.tell();
  f.close();
  if(verbose) console.log(`Wrote ${name}: ${ret} bytes`);
}

function WriteJSON(name, data) {
  WriteFile(name, JSON.stringify(data, null, 2));
}

function CreateREPL(prefix, suffix) {
  const repl = new REPL(`\x1b[38;5;165m${prefix} \x1b[38;5;39m${suffix}\x1b[0m`, null, false);

  repl.historyLoad(null, false);
  repl.loadSaveOptions();

  repl.help = () => {};
  let { log } = console;
  repl.show = arg => (typeof arg == 'string' ? arg : inspect(arg, globalThis.console.options));

  repl.cleanup = () => {
    repl.readlineRemovePrompt();
    let numLines = repl.historySave();

    repl.printStatus(`EXIT (wrote ${numLines} history entries)`, false);

    std.exit(0);
  };

  console.log = repl.printFunction(log);
  return repl;
}

function main(...args) {
  const base = scriptArgs[0]
    .replace(/.*\//g, '')
    .replace(/\.js$/gi, '')
    .replace(/\.[a-z]*$/, '');

  const config = ReadJSON(`.${base}-config`) ?? {};

  globalThis.console = new Console({
    inspectOptions: { compact: 10, customInspect: true, maxStringLength: 100 }
  });

  /* globalThis.console = {
    log(...args) {
      return console.log('X', ...args);
    },
    config: console.config,
    options: console.options
  };*/

  let params = (globalThis.params = getOpt(
    {
      help: [false, (_x, _y, opts) => showHelp(opts), 'h'],
      verbose: [false, (a, v) => (typeof v == 'number' ? v : 0) + 1, 'v'],
      listen: [false, null, 'l'],
      connect: [false, null, 'c'],
      client: [false, null, 'C'],
      server: [false, null, 'S'],
      debug: [false, null, 'x'],
      tls: [false, null, 't'],
      'no-tls': [false, (v, pv, o) => ((o.tls = false), true), 'T'],
      address: [true, null, 'a'],
      port: [true, null, 'p'],
      'ssl-cert': [true, null],
      'ssl-private-key': [true, null],
      '@': 'url'
    },
    args
  ));
  if(params['no-tls'] === true) params.tls = false;

  const {
    '@': [url = `ws://127.0.0.1:${params.port ?? 9090}/ws`],
    'ssl-cert': sslCert = 'localhost.crt',
    'ssl-private-key': sslPrivateKey = 'localhost.key'
  } = params;

  const listen = params.listen; //params.connect && !params.listen ? false : true;
  const serve = params.server; /* && !params.client*/

  /*console.log('listen', listen);
  console.log('serve', serve);*/

  let name = process.argv[1];
  name = name
    .replace(/.*\//, '')
    .replace(/-/g, ' ')
    .replace(/\.[^\/.]*$/, '');

  let repl = CreateREPL(...name.split(' '));
  let uri = new URL(url);
  //console.log('main', { url, uri });

  let ctor = () =>
    new RPCSocket(
      url,
      serve
        ? new RPCServer(
            FactoryEndpoint(
              {
                Directory,
                List,
                Location,
                Lexer,
                Location,
                SockAddr,
                Socket,
                AsyncSocket,
                StreamReader,
                StreamWriter,
                ReadableStream,
                ReadableStreamDefaultController,
                WritableStream,
                WritableStreamDefaultController,
                TransformStream
              },
              params.verbose
            ),
            params.verbose
          )
        : new FactoryClient(params.verbose),
      params.verbose
    );

  let socket = (globalThis.socket = ctor());

  //socket.register({ Array, Map });

  //globalThis[['connection', 'listener'][+listen]] = cli;

  define(globalThis, {
    get ws() {
      return socket.ws;
    },
    get connections() {
      return socket.connections;
    },
    get rpc() {
      const { connections } = socket;
      return connections[connections.length - 1];
    },
    get server() {
      const servers = socket.connections.filter(c => c instanceof RPCServer);

      return servers[servers.length - 1];
    },
    get client() {
      const clients = socket.connections.filter(c => c instanceof RPCClient);

      return clients[clients.length - 1];
    }
  });

  Object.assign(
    globalThis,
    {
      repl,
      quit,
      exit: quit,
      ReadJSON,
      WriteFile,
      WriteJSON
    },
    {
      MessageReceiver,
      MessageTransmitter,
      MessageTransceiver,
      codecs,
      RPCApi,
      RPCProxy,
      RPCObject,
      RPCFactory,
      RPCServer,
      RPCClient,
      RPCSocket,
      RPCConnect,
      RPCListen
    }
  );

  /* delete globalThis.DEBUG;
  Object.defineProperty(globalThis, 'DEBUG', { get: DebugFlags });*/

  const MakeWS = listen ? (url, callbacks) => createServer(url, callbacks) : (url, callbacks) => client(url, callbacks);

  listen ? socket.listen(MakeWS) : socket.connect(MakeWS);

  function quit(why) {
    console.log(`quit('${why}')`);

    let cfg = { inspectOptions: console.options };
    WriteJSON(`.${base}-config`, cfg);
    repl.cleanup(why);
  }

  repl.run();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error?.message ?? error}\n${error?.stack}`);
  std.exit(1);
} finally {
  //console.log('SUCCESS');
}
