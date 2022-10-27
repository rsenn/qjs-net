import { exit, puts } from 'std';
import { URL, LLL_INFO, LLL_USER, setLog } from 'net';
import Client from './client.js';
import { close, exec, open, O_RDWR, setReadHandler, setWriteHandler, Worker, ttySetRaw, sleep } from 'os';
import { in as stdin, out as stdout, err as stderr } from 'std';
import { assert, getpid, exists, randStr, abbreviate, escape } from './common.js';
import { spawn } from './spawn.js';
import { log } from './log.js';

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
