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
    args.push('https://localhost:30000/minnet.h');
  }

  for(let arg of args) {
    Client(
      arg,
      {
        onHttp(req, resp) {
          console.log('onHttp', { req, resp });

          let body = resp.text();

          puts(body);

          console.log(`Headers:`, resp.headers);
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
