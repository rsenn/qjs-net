import * as std from 'std';
import * as os from 'os';
import { assert, randStr } from './common.js';
import { spawn, wait4 } from './spawn.js';
import Client from './client.js';

function TestClient(url) {
  const message = randStr(100);

  return Client(url, {
    onConnect(ws, req) {
      ws.send(message);
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
      const exitCode = +!(`ECHO: ${message}` == msg);
      ws.close(1000 + exitCode);
      //std.exit(exitCode);
    }
  });
}

function main(...args) {
  let pid = spawn('server.js', 'localhost', 30000);
  let status = [];

  os.sleep(50);

  TestClient('ws://localhost:30000/ws');

  os.kill(pid, os.SIGTERM);
  wait4(pid, status, os.WNOHANG);
  console.log('status', status);
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  std.exit(1);
}
