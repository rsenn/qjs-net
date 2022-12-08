import { exit, puts } from 'std';
import { URL, LLL_INFO, LLL_USER, setLog } from 'net';
import Client from './client.js';
import { kill, close, exec, open, O_RDWR, setReadHandler, setWriteHandler, Worker, ttySetRaw, sleep } from 'os';
import { in as stdin, out as stdout, err as stderr } from 'std';
import { assert, getpid, exists, randStr, abbreviate, escape } from './common.js';
import { spawn, wait4 } from './spawn.js';
import { log } from './log.js';

function main(...args) {
  const debug = args.indexOf('-x') != -1;
  args = args.filter(arg => !/^-[x]/.test(arg));
  let pid;

  if(args.length == 0) {
    pid = spawn('server.js', ['localhost', 30000], null /*scriptArgs[0].replace(/.*\//g, '').replace('.js', '.log')*/);
    sleep(1000);
    args.push('https://localhost:30000/minnet.h');
  }

  for(let arg of args) {
    Client(
      arg,
      {
        onHttp(req, resp) {
          log('onHttp', { req, resp });

          let body = resp.text();

          puts(body);

          log(`Headers:`, resp.headers);
        }
      },
      debug ? LLL_INFO - 1 : LLL_USER
    );
  }

  function terminate(code = 0, ex = true) {
    let status;
    kill(pid, 9);
    wait4(pid, st => ((status = st), console.log(`exited: status=${status}`)));
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
