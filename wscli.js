#!/usr/bin/env qjsm
import * as std from 'std';
import * as os from 'os';
import REPL from 'repl';
import inspect from 'inspect';
import net, { Socket, URL } from 'net';
import { Console } from 'console';
import { quote } from 'util';

const connections = new Set();
let debug = 0;

function MakePrompt(prefix, suffix, commandMode = false) {
  return `\x1b[38;5;40m${prefix} \x1b[38;5;33m${suffix}\x1b[0m ${commandMode ? 'COMMAND' : 'DATA'} > `;
}

function FromDomain(buffer) {
  let s = '',
    i = 0,
    u8 = new Uint8Array(buffer);
  for(;;) {
    let len = u8[i++];
    if(len == 0) return s;
    if(s != '') s += '.';
    while(len--) s += String.fromCharCode(u8[i++]);
  }
}

function ToDomain(str, alpha = false) {
  return str
    .split('.')
    .reduce(
      alpha
        ? (a, s) => a + String.fromCharCode(s.length) + s
        : (a, s) => a.concat([s.length, ...s.split('').map(ch => ch.charCodeAt(0))]),
      alpha ? '' : []
    );
}

function DNSQuery(domain) {
  let type = 0x01;
  if(/^([0-9]+\.?){4}$/.test(domain)) {
    domain = domain.split('.').reverse().join('.') + '.in-addr.arpa';
    type = 0x0c;
  }
  console.log('DNSQuery', domain);
  let outBuf = new Uint8Array([
    0xff,
    0xff,
    0x01,
    0x00,
    0x00,
    0x01,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    ...ToDomain(domain),
    0x00,
    0x00,
    type,
    0x00,
    0x01
  ]).buffer;
  new DataView(outBuf).setUint16(0, outBuf.byteLength - 2, false);
  console.log('DNSQuery', outBuf);
  return outBuf;
}

function DNSResponse(buffer) {
  let u8 = new Uint8Array(buffer);
  let header = new DataView(buffer, 0, 12);
  let ofs = 2 + header.getUint16(0, false);
  header = new DataView(buffer, ofs, 12);
  let type = header.getUint16(2, false);
  ofs += 12;
  let addr;
  if(type == 0x0c) addr = FromDomain(buffer.slice(ofs));
  else addr = u8.slice(-4).join('.');
  return addr;
}

class CLI extends REPL {
  constructor(prompt2) {
    //console.log('process.argv', process.argv);
    let name = process.argv[1];
    name = name
      .replace(/.*\//, '')
      .replace(/-/g, ' ')
      .replace(/\.[^\/.]*$/, '');
    let [prefix, suffix] = [name, prompt2];
    let prompt = MakePrompt(prefix, suffix).slice(0, -2);
    super(prompt, false);

    Object.assign(this, { prefix, suffix });

    this.historyLoad(null, false);

    this.addCleanupHandler(() => {
      this.readlineRemovePrompt();
      this.printStatus(`EXIT`, false);
      std.exit(0);
    });
    let orig_log = console.log;
    let log = this.printFunction((...args) => orig_log(...args));
    console.log = (...args) => {
      //log('console.log:', args);
      //while(str.endsWith('\n')) str = str.slice(0, -1);

      this.printStatus(args.map(arg => inspect(arg, console.options)));
    };
    this.commandMode = false;
    this.commands['\x1b'] = this.escape;
    this.commands['§'] = this.escape;
    this.runSync();
  }

  help() {}

  show(arg) {
    std.puts((typeof arg == 'string' ? arg : inspect(arg, globalThis.console.options)) + '\n');
  }

  escape() {
    this.commandMode = !this.commandMode;
    this.readlineRemovePrompt();
    this.prompt = this.ps1 = MakePrompt(this.prefix, this.suffix, this.commandMode);
    this.readlinePrintPrompt();
  }

  handleCmd(data) {
    if(this.commandMode) return super.handleCmd(data);

    if(typeof data == 'string' && data.length > 0) {
      this.printStatus(`Sending '${data}'`, false);
      for(let connection of connections) connection.send(data);
    }
  }
}

function main(...args) {
  const base = scriptArgs[0].replace(/.*\//g, '').replace(/\.[a-z]*$/, '');
  globalThis.console = new Console({ inspectOptions: { depth: Infinity, compact: 1, customInspect: true } });
  let headers = [];
  let params = GetOpt(
    {
      verbose: [false, (a, v) => (v | 0) + 1, 'v'],
      listen: [false, null, 'l'],
      binary: [false, null, 'b'],
      connect: [false, null, 'c'],
      client: [false, null, 'C'],
      server: [false, null, 'S'],
      debug: [false, () => ++debug, 'x'],
      address: [true, null, 'a'],
      port: [true, null, 'p'],
      method: [true, null, 'm'],
      header: [
        true,
        arg => {
          const pos = arg.search(/: /);
          const name = arg.substring(0, pos);
          const value = arg.substring(pos + 2);

          headers.push([name, value]);
        },
        'H'
      ],
      'ssl-cert': [true, null],
      'ssl-private-key': [true, null],
      '@': 'url,'
    },
    args
  );
  const { 'ssl-cert': sslCert = 'localhost.crt', 'ssl-private-key': sslPrivateKey = 'localhost.key', method } = params;
  const url = params['@'][0] ?? 'ws://127.0.0.1:8999';
  const listen = params.connect && !params.listen ? false : true;
  const server = !params.client || params.server;
  const { binary } = params;

  function createWS(url, callbacks, listen = 0) {
    let repl,
      is_dns,
      urlObj = new URL(url);

    net.setLog(net.LLL_USER | (((debug ? net.LLL_INFO : net.LLL_NOTICE) << 1) - 1), (level, msg) => {
      let p =
        ['ERR', 'WARN', 'NOTICE', 'INFO', 'DEBUG', 'PARSER', 'HEADER', 'EXT', 'CLIENT', 'LATENCY', 'MINNET', 'THREAD'][
          level && Math.log2(level)
        ] ?? level + '';
      //console.log('log', { p, level,msg });

      if(/\[mux|__lws|\[wsicli|lws_/.test(msg)) return;
      msg = msg.replace(/\n/g, '\\n');

      if(params.verbose) std.puts(p.padEnd(8) + '\t' + msg + '\n');
    });

    const fn = [net.client, net.server][+listen];
    return fn(url, {
      sslCert,
      sslPrivateKey,
      method,
      binary,
      block: false,
      body: function* () {
        yield '{ "test": 1234 }';
      },
      headers: {
        'user-agent': 'minnet',
        ...Object.fromEntries(headers)
        //Connection: 'keep-alive'
        // Range: 'bytes=10-'
        //    'accept-encoding': 'br gzip',
      },
      ...callbacks,
      onConnect(ws, req) {
        connections.add(ws);

        Object.assign(globalThis, { ws, req });

        if(params.verbose) console.log('onConnect', { ws, req });
        const remote = `${ws.address}:${ws.port}`;
        try {
          repl = globalThis.repl = new CLI(remote);
        } catch(err) {
          console.log('error:', err.message);
        }
        repl.printStatus(`Connected to ${remote}`);
        const { url } = req;
        const { protocol, port } = url;
        if((is_dns = protocol == 'udp' && port == 53)) {
          ws.binary = true;
          ws.send(DNSQuery('libwebsockets.org'));
        }
      },
      onClose(ws, status, reason, error) {
        console.log('onClose', { ws, status, reason, error });
        connections.delete(ws);
        if(repl) {
          repl.printStatus(`Closed (${status}): ${reason}`);

          os.setTimeout(() => {
            //console.log('ws', ws);
            repl.exit(status != 1000 ? 1 : 0);
          }, 100);
        }
      },
      async onHttp(ws, req, resp) {
        console.log('onHttp', console.config({ compact: false }), { req, resp });
        console.log('request', req);
        console.log('request.headers', req.headers);
        console.log('response', resp);
        console.log('response.headers', resp.headers);
        let text = await resp.text();
        console.log('onHttp', text);
        /*text = text.replace(/\n/g, '\\n').replace(/\r/g, '\\r');
        const { url } = resp;
        console.log('onHttp', url, { text });

        let json = resp.json();
        console.log('onHttp', { json });
        let buffer = resp.arrayBuffer();
        console.log('onHttp', { buffer });*/
      },
      onFd(fd, rd, wr) {
        //console.log('onFd', fd, rd, wr);
        os.setReadHandler(fd, rd);
        os.setWriteHandler(fd, wr);
      },
      onMessage(ws, msg) {
        //console.log('onMessage', { ws });
        if(typeof msg == 'string') {
          msg = msg.replace(/\n/g, '\\n').replace(/\r/g, '\\r');
          msg = msg.substring(0, 100);
        }
        if(is_dns) {
          let response = DNSResponse(msg);
          //console.log('onMessage', { ws, response });
        } else {
          //console.log('onMessage', { ws, msg });
        }
      },
      onError(ws, error) {
        console.log('onError', ws, error);
      }
    });
  }
  Object.assign(globalThis, {
    get connections() {
      return [...connections];
    }
  });

  createWS(url, {})
    .then(() => {
      console.log('FINISHED');
    })
    .catch(err => {
      console.log('Failed', err);
    });

  function quit(why) {
    console.log(`quit('${why}')`);
    repl.cleanup(why);
  }
}

function GetOpt(options = {}, args) {
  let s, l;
  let r = {};
  let positional = (r['@'] = []);
  if(!(options instanceof Array)) options = Object.entries(options);
  const findOpt = a =>
    options.find(([optname, option]) => (Array.isArray(option) ? option.indexOf(a) != -1 : false) || a == optname);
  let [, params] = options.find(o => o[0] == '@') || [];
  if(typeof params == 'string') params = params.split(',');
  for(let i = 0; i < args.length; i++) {
    const a = args[i];
    let o;
    if(a[0] == '-') {
      let n, v, x, y;
      if(a[1] == '-') l = true;
      else s = true;
      x = s ? 1 : 2;
      if(s) y = 2;
      else if((y = a.indexOf('=')) == -1) y = a.length;
      n = a.substring(x, y);
      if((o = findOpt(n))) {
        const [has_arg, handler] = o[1];
        if(has_arg) {
          if(a.length > y) v = a.substring(y + (a[y] == '='));
          else v = args[++i];
        } else {
          v = true;
        }
        try {
          handler(v, r[o[0]], options, r);
        } catch(err) {}
        r[o[0]] = v;
        continue;
      }
    }
    if(params.length) {
      const p = params.shift();
      if((o = findOpt(p))) {
        const [, [, handler]] = o;
        let v = a;
        if(typeof handler == 'function')
          try {
            v = handler(v, r[o[0]], options, r);
          } catch(err) {}
        const n = o[0];
        r[o[0]] = v;
        continue;
      }
    }
    r['@'] = [...(r['@'] ?? []), a];
  }
  return r;
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  std.exit(1);
} finally {
  //console.log('SUCCESS');
}
