import { exit, puts, open } from 'std';
import { URL, LLL_ALL, LLL_INFO, LLL_USER, setLog } from 'net';
import Client from './client.js';
import { close, exec, O_RDWR, setReadHandler, setWriteHandler, Worker, ttySetRaw, sleep, kill, signal, SIGINT } from 'os';
import { in as stdin, out as stdout, err as stderr } from 'std';
import { assert, getpid, exists, randStr, abbreviate, escape, save } from './common.js';
import { spawn, wait4, WNOHANG } from './spawn.js';
import { log } from './log.js';

function main(...args) {
  const debug = args.indexOf('-x') != -1;
  args = args.filter(arg => !/^-[x]/.test(arg));
  let pid;

  if(args.length == 0) {
    pid = spawn(
      'server.js',
      ['localhost', 30000],
      null //      scriptArgs[0].replace(/.*\//g, '').replace('.js', '.log')
    );
    sleep(1000);
    args.push('wss://localhost:30000/ws');
  }

  /*setLog(
    LLL_ALL,
    (() => {
      let lf = open('test-client.log', 'w');
      return (level, msg) => {
        log(logLevels[level].padEnd(10) + msg);
        puts('LOG: ' + msg);
        lf.puts(logLevels[level].padEnd(10) + msg + '\n');
        lf.flush();
      };
    })()
  );*/

  (async function() {
    for(let arg of args) {
      let result, gen;

      result = Client(
        arg,
        {
          block: false,
          onConnect(ws, req) {
            log('onConnect', { ws, req });

            if(req?.url?.protocol) {
              const { protocol } = req.url;
              log('protocol', protocol);

              if(!protocol.startsWith('http')) {
                if(protocol.startsWith('ws')) {
                  setReadHandler(0, () => {
                    stdout.puts(`\r\x1b[0;37m>`);
                    stdout.flush();
                    let line = stdin.getline();

                    if(line.length) {
                      let s = line;
                      let result = ws.send(line);
                      log('result:', { result, s });
                      result.then(() => log('Sent:', { s }));
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
            }

            signal(SIGINT, () => {
              console.log('SIGINT', terminate(0, false));
              ws.close();
              exit(0);
            });
          },
          onClose(ws, status, reason, error) {
            log('onClose', { ws, status, reason, error });
            terminate(0);
          },
          onError(ws, error) {
            log('onError', { ws, error });
            terminate(1);
          },
          onMessage(ws, msg) {
            log('onMessage', { ws, msg });
            stdout.puts(`\r\x1b[1;34m< ${escape(msg)}\x1b[0m\n`);
            stdout.flush();
            // ws.close(1000);
          }
        },
        debug ? LLL_INFO - 1 : LLL_USER
      );

      log('result', result);
      log('result[Symbol.asyncIterator]', result[Symbol.asyncIterator]);

let file= await result.remoteName || 'output.txt';

log('file',file);

      save(result.readable, file/*?? 'output.txt'*/);

      /*for await(let chunk of result.readable)
        log('chunk', chunk.length);*/
    }
  })();

  function terminate(code = 0, ex = true) {
    let status;
    if(pid > 0) {
      kill(pid, 9);
      wait4(pid, st => ((status = st), console.log(`exited: status=${status}`)));
    }
    if(ex) exit(code);
    else return status;
  }
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  exit(1);
}
