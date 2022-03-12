import * as std from 'std';
import * as os from 'os';
import path from 'path';
import { Console } from 'console';
import REPL from 'repl';
import inspect from 'inspect';
import { types, define, filter, split, getOpt, toUnixTime } from 'util';
import * as fs from 'fs';
import { setLog, LLL_USER, LLL_NOTICE, LLL_WARN, client, server } from 'net';
import { Socket } from 'sockets';
import { EventEmitter } from 'events';
import { Repeater } from 'repeater';

import rpc from 'rpc';
import * as rpc2 from 'rpc';

globalThis.fs = fs;

function ReadJSON(filename) {
  let data = fs.readFileSync(filename, 'utf-8');

  if(data) console.debug(`${data.length} bytes read from '${filename}'`);
  return data ? JSON.parse(data) : null;
}

function WriteFile(name, data, verbose = true) {
  if(types.isGeneratorFunction(data)) {
    let fd = fs.openSync(name, os.O_WRONLY | os.O_TRUNC | os.O_CREAT, 0x1a4);
    let r = 0;
    for(let item of data) {
      r += fs.writeSync(fd, toArrayBuffer(item + ''));
    }
    fs.closeSync(fd);
    let stat = fs.statSync(name);
    return stat?.size;
  }
  if(types.isIterator(data)) data = [...data];
  if(types.isArray(data)) data = data.join('\n');

  if(typeof data == 'string' && !data.endsWith('\n')) data += '\n';
  let ret = fs.writeFileSync(name, data);

  if(verbose) console.log(`Wrote ${name}: ${ret} bytes`);
}

function WriteJSON(name, data) {
  WriteFile(name, JSON.stringify(data, null, 2));
}

function main(...args) {
  const base = path.basename(process.argv[1], '.js').replace(/\.[a-z]*$/, '');
  const config = ReadJSON(`.${base}-config`) ?? {};
  globalThis.console = new Console({ inspectOptions: { compact: 2, customInspect: true } });
  let params = getOpt(
    {
      verbose: [false, (a, v) => (v | 0) + 1, 'v'],
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
  );
  if(params['no-tls'] === true) params.tls = false;
  console.log('params', params);
  console.log('server', server);
  console.log('setLog', setLog);
  const {
    '@': [url = 'wss://127.0.0.1:8999/ws'],
    'ssl-cert': sslCert = 'localhost.crt',
    'ssl-private-key': sslPrivateKey = 'localhost.key'
  } = params;
  const listen = params.connect && !params.listen ? false : true;
  const server = !params.client || params.server;
  Object.assign(globalThis, { ...rpc2, rpc });
  let name = process.argv[1];
  name = name
    .replace(/.*\//, '')
    .replace(/-/g, ' ')
    .replace(/\.[^\/.]*$/, '');

  let [prefix, suffix] = name.split(' ');

  let repl = new REPL(`\x1b[38;5;165m${prefix} \x1b[38;5;39m${suffix}\x1b[0m`, fs, false);

  repl.historyLoad(null, false);

  repl.help = () => {};
  let { log } = console;
  repl.show = arg => std.puts((typeof arg == 'string' ? arg : inspect(arg, globalThis.console.options)) + '\n');

  repl.cleanup = () => {
    repl.readlineRemovePrompt();
    let numLines = repl.historySave();

    repl.printStatus(`EXIT (wrote ${numLines} history entries)`, false);

    std.exit(0);
  };

  console.log = repl.printFunction(log);
  let uri = new URL(url);
  console.log('main', { url, uri });

  let cli = (globalThis.sock = new rpc.Socket(uri, rpc[`RPC${server ? 'Server' : 'Client'}Connection`], +params.verbose));

  cli.register({ Socket, Worker: os.Worker, Repeater, REPL, EventEmitter });

  let connections = new Set();
  const createWS = (globalThis.createWS = (url, callbacks, listen) => {
    console.log('createWS', { url, callbacks, listen });
    const { protocol, host, port, path } = url;
    console.log('createWS', { protocol, host, port, path });
    setLog((params.debug ? LLL_USER : 0) | (((params.debug ? LLL_NOTICE : LLL_WARN) << 1) - 1), (level, ...args) => {
      repl.printStatus(...args);
      //if(params.debug) console.log((['ERR', 'WARN', 'NOTICE', 'INFO', 'DEBUG', 'PARSER', 'HEADER', 'EXT', 'CLIENT', 'LATENCY', 'MINNET', 'THREAD'][Math.log2(level)] ?? level + '').padEnd(8), ...args);
    });

    return [client, server][+listen]({
      protocol,
      host,
      port,
      path,
      tls: params.tls,
      sslCert,
      sslPrivateKey,
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
        },
        proxy(req, res) {
          const { url, method, headers } = req;
          const { status, ok, type } = res;

          console.log('proxy', { url, method, headers }, { status, ok, url, type });
        },
        *config(req, res) {
          console.log('/config', { req, res });
          yield '{}';
        },
        *files(req, resp) {
          const { body, headers } = req;
          const { 'content-type': content_type } = headers;
          const data = JSON.parse(body);

          resp.type = 'application/json';

          let { dir = 'tmp', filter = '.(brd|sch|G[A-Z][A-Z])$', verbose = false, objects = false, key = 'mtime' } = data;
          let absdir = path.realpath(dir);
          let components = absdir.split(path.sep);

          if(components.length && components[0] === '') components.shift();
          if(components.length < 2 || components[0] != 'home') throw new Error(`Access error`);

          console.log('\x1b[38;5;215m*files\x1b[0m', { dir, components, absdir });
          console.log('\x1b[38;5;215m*files\x1b[0m', { absdir });

          let names = fs.readdirSync(absdir) ?? [];
          if(filter) {
            const re = new RegExp(filter, 'gi');
            names = names.filter(name => re.test(name));
          }

          let entries = names.map(file => [file, fs.statSync(`${dir}/${file}`)]);

          entries = entries.reduce((acc, [file, st]) => {
            let name = file + (st.isDirectory() ? '/' : '');
            let obj = {
              name
            };
            acc.push([
              name,
              Object.assign(obj, {
                mtime: toUnixTime(st.mtime),
                time: toUnixTime(st.ctime),
                mode: `0${(st.mode & 0x09ff).toString(8)}`,
                size: st.size
              })
            ]);
            return acc;
          }, []);

          let cmp = {
            string(a, b) {
              return b[1][key].localeCompare(a[1][key]);
            },
            number(a, b) {
              return b[1][key] - a[1][key];
            }
          }[typeof entries[0][1][key]];

          entries = entries.sort(cmp);

          console.log('\x1b[38;5;215m*files\x1b[0m', { entries });
          names = entries.map(([name, obj]) => (objects ? obj : name));

          yield JSON.stringify(...[names, ...(verbose ? [null, 2] : [])]);
        }
      },
      ...url,

      ...callbacks,
      onConnect(ws, req) {
        console.log('test-rpc', { ws, req });
        connections.add(ws);

        return callbacks.onConnect(ws, req);
      },
      onClose(ws) {
        connections.delete(ws);

        return callbacks.onClose(ws, req);
      },
      onHttp(req, rsp) {
        const { url, method, headers } = req;
        console.log('\x1b[38;5;33monHttp\x1b[0m [\n  ', req, ',\n  ', rsp, '\n]');
        return rsp;
      },
      onMessage(ws, data) {
        console.log('onMessage', ws, data);
        return callbacks.onMessage(ws, data);
      },
      onFd(fd, rd, wr) {
        // console.log('onFd', { fd, rd, wr });
        return callbacks.onFd(fd, rd, wr);
      },
      ...(url && url.host ? url : {})
    });
  });
  globalThis[['connection', 'listener'][+listen]] = cli;

  define(globalThis, {
    get connections() {
      return [...connections];
    }
  });

  Object.assign(globalThis, {
    repl,
    ...rpc,
    quit,
    exit: quit,
    Socket,
    cli,
    net,
    std,
    os,
    fs,
    path,
    ReadJSON,
    WriteFile,
    WriteJSON
  });

  define(globalThis, listen ? { server: cli, cli } : { client: cli, cli });
  /* delete globalThis.DEBUG;
  Object.defineProperty(globalThis, 'DEBUG', { get: DebugFlags });*/

  if(listen) cli.listen(createWS, os);
  else cli.connect(createWS, os);

  function quit(why) {
    console.log(`quit('${why}')`);

    let cfg = { inspectOptions: console.options };
    WriteJSON(`.${base}-config`, cfg);
    repl.cleanup(why);
  }

  repl.runSync();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error?.message ?? error}\n${error?.stack}`);
  std.exit(1);
} finally {
  //console.log('SUCCESS');
}
