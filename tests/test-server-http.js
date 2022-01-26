import * as std from 'std';
import * as os from 'os';
import { randStr } from './common.js';
import { spawn, wait4 } from './spawn.js';
import Client from './client.js';

function TestClient(url) {
  const message = randStr(100);

  return Client(url, {
    onConnect(ws, req) {
      console.log('onConnect', { ws, req });
    },
    onClose(ws, reason) {
      console.log('onClose', { ws, reason });
      std.exit(1);
    },
    onError(ws, error) {
      console.log('onError', { ws, error });
      std.exit(1);
    },
    onHttp(req, resp) {
      console.log('onHttp', { req, resp });

      let file = std.loadFile('.' + req.path);

      let body = resp.text();
      console.log('onHttp', { body, file });

      if(file.length == body.length) if (file === body) std.exit(0);
    }
  });
}

function main(...args) {
  let pid = spawn('server.js', 'localhost', 30000);
  let status = [];

  os.sleep(50);

  TestClient('http://localhost:30000/jsutils.h');

  wait4(pid, status);
  console.log('status', status);
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  std.exit(1);
}
