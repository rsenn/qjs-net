#!/usr/bin/env qjsm
import { client, createServer, LLL_INFO, LLL_NOTICE, LLL_USER, Request, setLog, URL } from 'net.so';
import * as os from 'os';
import * as std from 'std';

const connections = new Set();

let debug = 0,
  params,
  repl,
  command = false;

function MakePrompt(prefix, suffix, commandMode = false) {
  return `\x1b[38;5;40m${prefix} \x1b[38;5;33m${suffix}\x1b[0m ${commandMode ? 'COMMAND' : 'DATA'}`;
}

function GetPrompt(prompt2) {
  let name = scriptArgs[0]
    .replace(/.*\//, '')
    .replace(/-/g, ' ')
    .replace(/\.[^\/.]*$/, '');

  let [prefix, suffix] = [name, prompt2];

  return MakePrompt(prefix, suffix, command);
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

function WriteFile(filename, buffer) {
  let fd;
  let err = {};
  if((fd = std.open(filename, 'w+', err))) {
    let r = typeof buffer == 'string' ? (fd.puts(buffer), fd.tell()) : fd.write(buffer, 0, buffer.byteLength);
    fd.close();
    console.log(`r`, r);
    if(r >= 0) console.log(`Wrote '${filename}'.`);
    else console.log(`Error writing '${filename}': ${std.strerror(err.errno)}`);
    return r;
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

class CLI {
  constructor(prompt) {
    this.prompt = prompt + '> ';
  }

  getline() {
    std.out.puts('\x1b[2K\x1b[1G');
    std.out.puts(this.prompt);
    std.out.flush();

    return readLine(std.in);

    function waitRead(file) {
      return new Promise(
        (fd => (resolve, reject) => {
          os.setReadHandler(fd, () => {
            os.setReadHandler(fd, null);
            resolve(file);
          });
        })(file.fileno())
      );
    }

    async function readLine(file) {
      await waitRead(file);
      return file.getline();
    }
  }

  async run(callback) {
    for(;;) {
      let line = await this.getline();

      if(line === null) break;
      if(line === '') continue;

      callback(line);
    }
    std.exit(0);
  }

  printStatus(...args) {
    std.out.puts('\x1b[2K\x1b[1G');
    std.out.flush();
    console.log(...args);
    std.out.puts(this.prompt);
    std.out.flush();
  }
}

async function main(...args) {
  let headers = [];
  params = GetOpt(
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
      output: [true, null, 'o'],
      protocol: [true, null, 'P'],
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
  const listen = params.connect && !params.listen ? false : true;
  const server = !params.client || params.server;
  const { binary, protocol } = params;
  let urls = params['@'];

  function createWS(url, callbacks, listen = 0) {
    let repl;
    let is_dns,
      urlObj = new URL(url);

    setLog(LLL_USER | (((debug ? LLL_INFO : LLL_NOTICE) << 1) - 1), (level, msg) => {
      let p =
        ['ERR', 'WARN', 'NOTICE', 'INFO', 'DEBUG', 'PARSER', 'HEADER', 'EXT', 'CLIENT', 'LATENCY', 'MINNET', 'THREAD'][
          level && Math.log2(level)
        ] ?? level + '';
      if(p == 'INFO' || /RECEIVE_CLIENT_HTTP_READ|\[mux|__lws|\[wsicli|lws_/.test(msg)) return;
      msg = msg.replace(/\n/g, '\\n');
      if(params.verbose > 1 || params.debug) std.puts(p.padEnd(8) + '\t' + msg + '\n');
    });

    if(params.verbose) console.log(`Connecting to '${url}'...`);

    globalThis.PrintMessage = PrintMessage;

    const fn = [client, createServer][+listen];

    return fn(url, {
      sslCert,
      sslPrivateKey,
      method,
      binary,
      protocol,
      block: false,
      body: function* () {
        yield '{ "test": 1234 }';
      },
      headers: {
        'user-agent': 'minnet',
        ...Object.fromEntries(headers)
      },
      ...callbacks,
      async onConnect(ws, req) {
        if(params.verbose) console.log('onConnect', { ws, req });
        connections.add(ws);
        Object.assign(globalThis, { ws, req });

        const remote = `${ws.address}:${ws.port}`;

        try {
          const module = await import('/home/roman/Projects/plot-cv/quickjs/qjs-modules/lib/repl.js').catch(() => ({
            REPL: CLI
          }));

          repl = globalThis.repl = new module.REPL(GetPrompt(remote));

          repl.run(data => {
            if(command) return repl.evalAndPrint(data);
            if(typeof data == 'string' && data.length > 0) {
              console.log(`Sending '${data}'`);
              for(let connection of connections) {
                connection.send(data);
              }
            }
          });

          repl.commands['§'] = () => {
            command = !command;
            repl.readlineRemovePrompt();
            repl.prompt = repl.ps1 = GetPrompt(remote) + '> ';
            repl.readlinePrintPrompt();
          };
        } catch(err) {
          console.log('error:', err.message + '\n' + err.stack);
        }

        // console.log('onConnect', { remote, repl });

        repl.printStatus(`Connected to ${remote}`);
        if(req) {
          const { url } = req;
          const { protocol, port } = url;
          if((is_dns = protocol == 'udp' && port == 53)) {
            ws.binary = true;
            ws.send(DNSQuery('libwebsockets.org'));
          }
        }
      },
      onClose(ws, status, reason, error) {
        if(error) {
          quit(`Connection error: ${reason}`);
        }

        repl.printStatus('onClose', { ws, status, reason, error });
        connections.delete(ws);
        if(repl) {
          repl.printStatus(`Closed (${status}): ${reason}`);
          os.setTimeout(() => {
            repl.exit(status != 1000 ? 1 : 0);
          }, 100);
        }
      },
      onRequest(req, resp) {
        console.log('onRequest', console.config({ compact: false }), { req, resp });
        let { headers } = resp;

        let type = (headers['content-type'] ?? 'text/html').replace(/;.*/g, '');
        let extension = '.' + type.replace(/.*\//g, '');
        let { url } = req || {};

        if(url) {
          let { path } = url;
          let name = path.replace(/\/[a-z]\/.*/g, '').replace(/.*\//g, '');

          if(name == '') name = 'index';
          if(!name.endsWith(extension)) name += extension;

          let buffer = resp.body;
          // let text = toString(buffer);
          console.log('onRequest', { buffer });

          WriteFile(params.output ?? name ?? 'output.bin', buffer);
        }

        let next = urls.length && urls.shift();

        if(next) {
          req = new Request(next);
          return req;
        } else {
          return -1;
        }
      },
      onFd(fd, rd, wr) {
        os.setReadHandler(fd, rd);
        os.setWriteHandler(fd, wr);
      },
      onMessage(ws, msg, first, final) {
        globalThis.msg = msg;

        if(typeof globalThis.onMessage == 'function') {
          globalThis.onMessage(msg);
          return;
        }

        //globalThis.onMessage(msg);
        PrintMessage(msg);
        //        console.log('onMessage', console.config({ compact: 1 }), msg);
        //startInteractive();

        if(typeof msg == 'string') {
          msg = msg.replace(/\n/g, '\\n').replace(/\r/g, '\\r');
          msg = msg.substring(0, 100);
        }

        if(is_dns) {
          let response = DNSResponse(msg);
        } else {
        }
      },
      onError(ws, error) {}
    });

    function PrintMessage(msg) {
      try {
        if(/^{.*}\s*$/gm.test(msg)) {
          let obj = JSON.parse(msg);
          msg = inspect(obj, { colors: true, depth: Infinity, compact: 3 });
        }
      } catch(e) {}

      repl.printStatus('Message: ' + msg);
    }
    Object.assign(globalThis, {
      get connections() {
        return [...connections];
      }
    });
  }

  try {
    let instance = createWS(urls.shift(), {});

    console.log('instance', instance, instance[Symbol.asyncIterator]);
  } catch(error) {
    quit('ERROR: ' + error.message);
  }

  function quit(why) {
    if(why) std.err.puts(why + '\n');
    if(repl) repl.cleanup(why);
    std.exit(0);
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
}
