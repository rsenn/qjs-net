import { exit, loadFile } from 'std';
import { kill, SIGTERM, sleep, WNOHANG } from 'os';
import { randStr } from './common.js';
import { spawn, wait4 } from './spawn.js';
import Client from './client.js';
import { log } from './log.js';

function TestClient(url) {
  const message = randStr(100);

  return Client(url, {
    onConnect(ws, req) {
      log('onConnect', { ws, req });
    },
    onClose(ws, reason) {
      log('onClose', { ws, reason });
      exit(reason);
    },
    onError(ws, error) {
      log('onError', { ws, error });
      exit(1);
    },
    onHttp(req, resp) {
      const { url } = resp;
      log('onHttp', { req, resp });
      log('req.url', req.url);
      log('resp.url', resp.url);
      log('url.path', url.path);

      let file = loadFile('.' + url.path);

      let body = resp.text();
      log('onHttp', { body, file });

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
  log('status', status);
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  exit(1);
}
