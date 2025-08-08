import { LLL_INFO, LLL_USER } from 'net.so';
import { kill, sleep } from 'os';
import Client from './client.js';
import { escape, save } from './common.js';
import { log } from './log.js';
import { spawn, wait4 } from './spawn.js';
import { exit, out as stdout, puts } from 'std';

async function main(...args) {
  const debug = args.indexOf('-x') != -1;
  args = args.filter(arg => !/^-[x]/.test(arg));
  let pid;

  if(args.length == 0) {
    pid = spawn('server.js', ['localhost', 30000], null /*scriptArgs[0].replace(/.*\//g, '').replace('.js', '.log')*/);
    sleep(1000);
    args.push('https://localhost:30000/src/minnet.h');
  }

  for(let arg of args) {
    let cli;

    cli = Client(
      arg,
      {
        block: false,
        onRequest(req, resp) {
          log('onRequest', { req, resp });

          /*  let body = resp.text();
          puts(body);*/
          log(`Headers:`, resp.headers);
        },
        onMessage(...args) {
          log('onMessage', { args });
          /* stdout.puts(`\r\x1b[1;34m< ${escape(msg)}\x1b[0m\n`);
            stdout.flush();*/
        },
      },
      1, // debug ? LLL_INFO - 1 : LLL_USER
    );

    console.log('cli', cli);
    console.log('cli.readable', cli.readable);

    save(cli.readable, 'output.txt');

    /*for await(let chunk of cli.readable) {
      console.log(
        'DATA: ' +
          chunk
            .replace(/\n/g, '\n      ')
            .split('\n')
            .map(line => line.substring(0, 80))
            .join('\n')
      );
    }*/
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
