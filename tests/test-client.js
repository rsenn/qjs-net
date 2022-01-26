import { exit, puts } from 'std';
import { URL } from 'net';
import Client from './client.js';
import { close, exec, open, O_RDWR, setReadHandler, setWriteHandler, Worker, ttySetRaw } from 'os';
import { in as stdin, out as stdout, err as stderr } from 'std';
import { assert, getpid, exists, randStr, abbreviate, escape } from './common.js';

function main(...args) {
  const debug = args.indexOf('-x') != -1;
  args = args.filter(arg => !/^-[x]/.test(arg));

  if(args.length == 0) args.push('https://localhost/debugger.html');

  for(let arg of args) {
    Client(
      arg,
      {
        onConnect(ws, req, resp) {
          console.log('onConnect', { ws, req, resp });
          const { protocol } = new URL(req.url);
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
          //console.log('onMessage', { ws, msg });
          stdout.puts(`\r\x1b[1;34m< ${escape(msg)}\x1b[0m\n`);
          stdout.flush();
          // ws.close(1000);
        },
        onHttp(req, resp) {
          console.log('onHttp', { req, resp });

          let body = resp.text();

          puts(body);

          console.log(`Headers:`, resp.headers);
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
  exit(1);
}
