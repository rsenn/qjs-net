import { exit, loadFile } from 'std';
import { kill, SIGTERM, sleep, WNOHANG } from 'os';
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
      exit(reason);
    },
    onError(ws, error) {
      console.log('onError', { ws, error });
      exit(1);
    },
    onHttp(req, resp) {
      const { url } = resp;
      console.log('onHttp', { req, resp });
      console.log('req.url', req.url);
      console.log('resp.url', resp.url);
      console.log('url.path', url.path);

      let file = loadFile('.' + url.path);

      let body = resp.text();
      console.log('onHttp', { body, file });

      if(file.length == body.length) if (file === body) exit(0);
    }
  });
}

function main(...args) {
  let pid = spawn('server.js', ['localhost', 30000], scriptArgs[0].replace(/.*\//g, '').replace('.js', '.log'));
  let status = [];

  sleep(100);

  TestClient('https://localhost:30000/minnet.h');

  kill(pid, SIGTERM);
  wait4(pid, status, WNOHANG);
  console.log('status', status);
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  exit(1);
}
