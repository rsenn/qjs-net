import { LLL_INFO } from 'net.so';
import { LLL_USER } from 'net.so';
import { close } from 'os';
import { sleep } from 'os';
import Client from './client.js';
import { log } from './log.js';
import { spawn } from './spawn.js';
import { exit } from 'std';
function main(...args) {
  const debug = args.indexOf('-x') != -1;
  args = args.filter(arg => !/^-[x]/.test(arg));
  let pid;

  if(args.length == 0) {
    pid = spawn('server.js', ['localhost', 30000], scriptArgs[0].replace(/.*\//g, '').replace('.js', '.log'));
    sleep(100);
    args.push('wss://localhost:30000/ws');
  }

  for(let arg of args) {
    Client(
      arg,
      {
        onConnect(ws, req) {
          log('onConnect', { ws, req });

          ws.send('TEST STRING');
          ws.send(new Uint8Array([0x54, 0x45, 0x53, 0x54, 0x20, 0x44, 0x41, 0x54, 0x41]).buffer);
          ws.send('QUIT');
        },
        onClose(ws, reason) {
          log(`onClose ${reason}`);
          exit(0);
        },
        onError(ws, error) {
          log(`onError '${error}'`);
          exit(1);
        },
        onMessage(ws, msg) {
          log(`onMessage '${msg}'`);

          if(msg.endsWith('QUIT')) {
            log(`CLOSING!`);
            ws.close(1001);
          }
        }
      },
      debug ? LLL_INFO - 1 : LLL_USER
    );
  }
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  exit(1);
}