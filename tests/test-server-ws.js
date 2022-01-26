import * as std from 'std';
import * as os from 'os';
import net, { LLL_ERR, LLL_WARN, LLL_NOTICE, LLL_INFO, LLL_CLIENT, LLL_LATENCY, LLL_USER, LLL_THREAD } from 'net';
import { Console } from 'console';
import { MinnetServer, MakeCert } from './server.js';
import { TestFetch } from './fetch.js';
import { assert, randStr } from './common.js';
import { getexe, thisdir, spawn, wait4 } from './spawn.js';
import { Levels, DefaultLevels, Init } from './log.js';
import Client from './client.js';

function TestClient(url = 'ws://localhost:30000/ws') {
  const sslCert = 'localhost.crt',
    sslPrivateKey = 'localhost.key';

  const message = randStr(100);

  return Client(url, {
    sslCert,
    sslPrivateKey,

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
      // assert(`ECHO:${message}`, msg);
      console.log('onMessage', { ws, msg });
      ws.close(1008);
    }
  });
}

function main(...args) {
  let pid = spawn('server.js', 'localhost', 30000);
  let status = [];
  console.log('pid', pid);

  os.sleep(100);

  TestClient();

  console.log(`wait4`, { pid, status });
  console.log(`wait4`, '=', wait4(pid, status));
  console.log('status', status);
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  std.exit(1);
}
