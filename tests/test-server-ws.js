import { kill, SIGTERM, sleep, WNOHANG } from 'os';
import Client from './client.js';
import { randStr } from './common.js';
import { log } from './log.js';
import { spawn, wait4 } from './spawn.js';
import { exit } from 'std';
function TestClient(url) {
  const message = randStr(100);

  return Client(url, {
    onConnect(ws, req) {
      log('onConnect', { ws, req });
      ws.send(message);
    },
    onClose(ws, reason) {
      log('onClose', { ws, reason });
      exit(0);
    },
    onError(ws, error) {
      log('onError', { ws, error });
      exit(1);
    },
    onMessage(ws, msg) {
      log('onMessage', { ws, msg });
      const exitCode = +!(`ECHO: ${message}` == msg);
      ws.close(1000 + exitCode);
    }
  });
}

function main(...args) {
  let pid = spawn('server.js', ['localhost', 30000], scriptArgs[0].replace(/.*\//g, '').replace('.js', '.log'));
  let status = [];

  sleep(100);

  TestClient('ws://localhost:30000/ws');

  kill(pid, SIGTERM);
  wait4(pid, status, WNOHANG);
  log('status', status);
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  exit(1);
}
