import * as std from 'std';
import * as os from 'os';
import net, { LLL_ERR, LLL_WARN, LLL_NOTICE, LLL_INFO, LLL_CLIENT, LLL_LATENCY, LLL_USER, LLL_THREAD } from 'net';
import { Console } from 'console';
import { MinnetServer, MakeCert } from './server.js';
import { TestFetch } from './fetch.js';
import assert from './assert.js';
import { getexe, thisdir, spawn } from './spawn.js';
import { Levels, DefaultLevels, Init } from './log.js';
import Client from './client.js';

function TestClient(url = 'ws://localhost:30000/ws') {
  const sslCert = 'localhost.crt',
    sslPrivateKey = 'localhost.key';

  Init('TestClient');

  return Client(url, { sslCert, sslPrivateKey,

  onConnect(ws,req) {
    ws.send("HELLO\r\n");
  } });
}

function main(...args) {
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
