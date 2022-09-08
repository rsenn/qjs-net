import { exit, puts } from 'std';
import { URL, LLL_INFO, LLL_USER, setLog } from 'net';
import Client from './client.js';
import { close, exec, open, O_RDWR, setReadHandler, setWriteHandler, Worker, ttySetRaw, sleep } from 'os';
import { in as stdin, out as stdout, err as stderr } from 'std';
import { assert, getpid, exists, randStr, abbreviate, escape } from './common.js';
import { spawn } from './spawn.js';

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
          console.log('onConnect', { ws, req });
          const { protocol } = req.url;
          console.log('protocol', protocol);

          if(!protocol.startsWith('http')) {
            if(protocol.startsWith('ws')) {
              setReadHandler(0, () => {
                stdout.puts(`\r\x1b[0;37m>`);
                stdout.flush();
                let line = stdin.getline();

                if(line.length) {
                  ws.send(line);
                  stdout.puts(`\x1b[0m\n`);
                  stdout.flush();
                }
              });
            } else {
              ttySetRaw(0);
              setReadHandler(0, () => {
                let b = stdin.getByte();
                if(b == 13) b = 10;
                else if(b == 127) b = 8;
                else if(b < 32 || b > 'z'.charCodeAt(0)) stdout.puts('char: ' + b);
                stdout.putByte(b);
                stdout.flush();

                ws.send(String.fromCharCode(b));
              });
            }
          }
        },
        onClose(ws, reason) {
          console.log('onClose', { ws, reason });
          exit(0);
        },
        onError(ws, error) {
          console.log('onError', { ws, error });
          exit(1);
        },
        onMessage(ws, msg) {
          console.log('onMessage', { ws, msg });
          stdout.puts(`\r\x1b[1;34m< ${escape(msg)}\x1b[0m\n`);
          stdout.flush();
          // ws.close(1000);
        }
      },
      debug ? LLL_INFO - 1 : LLL_USER
    );
  }
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  exit(1);
}
