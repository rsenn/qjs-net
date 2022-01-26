import * as std from 'std';
import * as os from 'os';
import net, { URL } from 'net';
import { Levels, DefaultLevels, Init, SetLog } from './log.js';
import Client from './client.js';

function main(...args) {
  const debug = args.indexOf('-x') != -1;
  args = args.filter(arg => !/^-[x]/.test(arg));

  if(args.length == 0) args.push('https://github.com/rsenn?tab=repositories');

  for(let arg of args) {
    Client(
      arg,
      {
        onConnect(ws, req) {
          console.log('onConnect', { ws, req });
        },
        onClose(ws, reason) {
          console.log('onClose', { ws, reason });
          std.exit(0);
        },
        onError(ws, error) {
          console.log('onError', { ws, error });
          std.exit(1);
        },
        onMessage(ws, msg) {
          console.log('onMessage', { ws, msg });
          std.puts(msg);
          ws.close(1008);
        },
        onHttp(req, resp) {
          console.log('onHttp', { req, resp });
          let body = resp.text();

          std.puts(body);
        }
      },
      debug
    );
  }
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  std.exit(1);
}
