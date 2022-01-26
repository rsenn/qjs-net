import * as std from 'std';
import * as os from 'os';
import net, { LLL_ERR, LLL_WARN, LLL_NOTICE, LLL_INFO, LLL_CLIENT, LLL_LATENCY, LLL_USER, LLL_THREAD } from 'net';
import { Console } from 'console';
import { MinnetServer, MakeCert } from './server.js';
import { TestFetch } from './fetch.js';
import assert from './assert.js';
import { getexe, thisdir, spawn } from './spawn.js';
import { Levels, DefaultLevels, Init } from './log.js';

function TestClient(url = 'ws://localhost:30000/ws') {
  
Init('TestClient');

  return net.client(url, {
    onConnect(ws, req) {
      console.log('onConnect', { ws, req });
    },
    onError(ws, error) {
      console.log('onError', { ws, error });
    },
    onClose(ws, reason) {
      console.log('onClose', { ws, reason });
    },
    onMessage(ws, msg) {
      console.log('onMessage', { ws, msg });
    }
  });
}

function main(...args) {
  globalThis.console = new Console({ inspectOptions: { compact: 2, customInspect: true, maxStringLength: 100 } });

  let pid = spawn('server.js', 'localhost', 30000);
  console.log('pid', pid);

  os.sleep(1000);

  TestClient();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  std.exit(1);
}
