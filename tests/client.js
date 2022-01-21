import * as std from 'std';
import * as os from 'os';
import inspect from 'inspect';
import * as net from 'net';
import { Console } from 'console';

const escape = s =>
  [
    [/\r/g, '\\r'],
    [/\n/g, '\\n']
  ].reduce((a, [exp, rpl]) => a.replace(exp, rpl), s);
const abbreviate = s => (s.length > 100 ? s.substring(0, 45) + ' ... ' + s.substring(-45) : s);

const connections = new Set();

function main(...args) {
  const base = scriptArgs[0].replace(/.*\//g, '').replace(/\.[a-z]*$/, '');
  globalThis.console = new Console({ inspectOptions: { compact: 2, customInspect: true, maxStringLength: 100 } });

  const sslCert = 'localhost.crt',
    sslPrivateKey = 'localhost.key';

const debug = args[0] == '-x' && args.shift();
  if(args.length == 0) args.push('https://github.com/rsenn?tab=repositories');

  function createWS(url, callbacks, listen = 0) {
    let matches = [...url.matchAll(/([:\\/]+|[^:\\/]+)/g)].map(a => a[0]);
    let [protocol, , host] = matches.splice(0, 3);
    let port;
    if((matches[0] == ':' && (matches.shift(), true)) || !isNaN(+matches[0])) port = +matches.shift();
    else port = { https: 443, http: 80 }[protocol];

    let path = matches.join('');
    let [location, query] = path.split(/\?/);

    net.setLog(/* net.LLL_USER |*/ (net.LLL_WARN << 1) - 1, debug ?  (level, msg) => {
      const l = ['ERR', 'WARN', 'NOTICE', 'INFO', 'DEBUG', 'PARSER', 'HEADER', 'EXT', 'CLIENT', 'LATENCY', 'MINNET', 'THREAD'][level && Math.log2(level)] ?? level + '';
     if(l == 'NOTICE' || l=='MINNET' ) //if(l != 'NOTICE' ) if(l != 'MINNET') if(!/POLL/.test(msg))
      console.log(('X', l).padEnd(8), msg.replace(/\r/g, '\\r').replace(/\n/g, '\\n'));
    }: () => {});

    //console.log('createWS', { protocol, host, port, location, listen });

    const fn = [net.client, net.server][+listen];
    return fn({
      block: false,
      sslCert,
      sslPrivateKey,
      protocol,
      ssl: protocol == 'wss',
      host,
      port,
      path,
      ...callbacks,
      onConnect(ws, req) {
        console.log('onConnect', ws, req);
        connections.add(ws);
        try {
          console.log(`Connected to ${protocol}://${host}:${port}${path}`, true);
        } catch(err) {
          console.log('error:', err.message);
        }
      },
      onClose(ws, status) {
        connections.delete(ws);
        console.log('onClose', { ws, status });
        std.exit(status != 1000 ? 1 : 0);
      },
      onError(ws, error) {
        console.log('onError', { ws, error });
        std.exit(error);
      },
      onHttp(req, rsp) {
        const { url, method, headers } = req;
        console.log('\x1b[38;5;82monHttp\x1b[0m(\n\t', Object.setPrototypeOf({ url, method, headers }, Object.getPrototypeOf(req)), ',\n\t', rsp, '\n)');
        return rsp;
      },
      onFd(fd, rd, wr) {
        // console.log('onFd', fd, rd, wr);
        os.setReadHandler(fd, rd);
        os.setWriteHandler(fd, wr);
      },
      onMessage(ws, msg) {
        console.log('onMessage', console.config({ maxStringLen: 100 }), { ws, msg });

        std.puts(escape(abbreviate(msg)) + '\n');
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

  for(let arg of args) {
    createWS(arg, {}, false);
  }

  function quit(why) {
    console.log(`quit('${why}')`);
    std.exit(0);
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
