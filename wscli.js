import * as std from 'std';
import * as os from 'os';
import REPL from 'repl';
import inspect from 'inspect';
import net, { Socket, URL } from 'net';
import { Console } from 'console';

const connections = new Set();

function GetOpt(options = {}, args) {
  let s, l;
  let r = {};
  let positional = (r['@'] = []);
  if(!(options instanceof Array)) options = Object.entries(options);
  const findOpt = a => options.find(([optname, option]) => (Array.isArray(option) ? option.indexOf(a) != -1 : false) || a == optname);
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

class CLI extends REPL {
  constructor(prompt2) {
    //console.log('process.argv', process.argv);
    let name = process.argv[1];
    name = name
      .replace(/.*\//, '')
      .replace(/-/g, ' ')
      .replace(/\.[^\/.]*$/, '');
    let [prefix, suffix] = [name, prompt2];

    super(`\x1b[38;5;40m${prefix} \x1b[38;5;33m${suffix}\x1b[0m`, false);

    this.historyLoad(null, false);

    this.addCleanupHandler(() => {
      this.readlineRemovePrompt();
      this.printStatus(`EXIT`, false);
      std.exit(0);
    });
    let log = this.printFunction(console.log);
    console.log = (...args) => {
      //log('console.log:', args);
      log(...args);
    };
    this.runSync();
  }

  help() {}

  show(arg) {
    std.puts((typeof arg == 'string' ? arg : inspect(arg, globalThis.console.options)) + '\n');
  }

  handleCmd(data) {
    if(typeof data == 'string' && data.length > 0) {
      this.printStatus(`Sending '${data}'`, false);
      for(let connection of connections) connection.send(data);
    }
  }
}

function main(...args) {
  const base = scriptArgs[0].replace(/.*\//g, '').replace(/\.[a-z]*$/, '');
  globalThis.console = new Console({ inspectOptions: { compact: 0, customInspect: true } });
  let params = GetOpt(
    {
      verbose: [false, (a, v) => (v | 0) + 1, 'v'],
      listen: [false, null, 'l'],
      connect: [false, null, 'c'],
      client: [false, null, 'C'],
      server: [false, null, 'S'],
      debug: [false, null, 'x'],
      address: [true, null, 'a'],
      port: [true, null, 'p'],
      method: [true, null, 'm'],
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
  //console.log('params', params);
  function createWS(url, callbacks, listen = 0) {
    let urlObj = new URL(url);

    net.setLog(((params.debug ? net.LLL_DEBUG : net.LLL_WARN) << 1) - 1, (level, msg) => {
      let p = ['ERR', 'WARN', 'NOTICE', 'INFO', 'DEBUG', 'PARSER', 'HEADER', 'EXT', 'CLIENT', 'LATENCY', 'MINNET', 'THREAD'][level && Math.log2(level)] ?? level + '';
      msg = msg.replace(/\n/g, '\\n').replace(/\r/g, '\\r');

      if(!/POLL/.test(msg) && /MINNET/.test(p)) if (params.debug && /(client|http|read|write)/i.test(msg)) console.log(p.padEnd(8), msg);
    });

    let repl;

    const fn = [net.client, net.server][+listen];
    //console.log('createWS', {url, repl, fn });
    return fn(url, {
      sslCert,
      sslPrivateKey,
      method,
      body: (function* () {
        yield '{ "test": 1234 }';
      })(),
      headers: {
        'user-agent': 'minnet'
        /*'Content-Type': 'application/json',
        'Content-Length': 1000*/
        //'Connection': 'keep-alive',
        // Range: 'bytes=10-'
        //    'accept-encoding': 'br gzip',
      },
      ...callbacks,
      onConnect(ws, req) {
        connections.add(ws);
        //console.log('onConnect', { ws, req });
        const { address, port } = ws;
const remote=`${address}:${port}`;
        try {
          repl = new CLI(remote);
        } catch(err) {
          console.log('error:', err.message);
        }
        //        const {url}= req;
        repl.printStatus(`Connected to ${remote}`);
      },
      onClose(ws, status, reason, error) {
        connections.delete(ws);
        repl.printStatus(`Closed (${status}): ${reason}`);

        os.setTimeout(() => {
          //console.log('ws', ws);
          repl.exit(status != 1000 ? 1 : 0);
        }, 100);
      },
      onHttp(req, resp) {
        console.log('onHttp', { req, resp });
        let text = resp.text();
        text = text.replace(/\n/g, '\\n').replace(/\r/g, '\\r');
        console.log('onHttp', { text });

        /* let json =resp.json();
        console.log('onHttp', { json }); */
        let buffer = resp.arrayBuffer();
        console.log('onHttp', { buffer });
      },
      onFd(fd, rd, wr) {
        //console.log('onFd', fd, rd, wr);
        os.setReadHandler(fd, rd);
        os.setWriteHandler(fd, wr);
      },
      onMessage(ws, msg) {
        msg = msg.replace(/\n/g, '\\n').replace(/\r/g, '\\r');
        console.log('onMessage', { ws, msg: msg.substring(0, 100) });
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
  createWS(url, {});
  function quit(why) {
    console.log(`quit('${why}')`);
    repl.cleanup(why);
  }
}
try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  std.exit(1);
} finally {
  //console.log('SUCCESS');
}
