import * as std from 'std';
import * as os from 'os';
import { Console } from 'console';
import { MinnetServer, MakeCert } from './server.js';
import { TestFetch } from './fetch.js';
import { assert } from './common.js';

function main(...args) {
  const sslCert = 'localhost.crt',
    sslPrivateKey = 'localhost.key';

  const host = 'localhost',
    port = 30000;

  let server = new MinnetServer({
    host,
    port,
    protocol: 'http',
    sslCert,
    sslPrivateKey,
    mounts: {
      *generator(req, res) {
        console.log('/generator', { req, res });
        yield 'This';
        yield ' ';
        yield 'is';
        yield ' ';
        yield 'a';
        yield ' ';
        yield 'generated';
        yield ' ';
        yield 'response';
        yield '\n';
      }
    }
  });

  let fetch = TestFetch(host, port);

  let worker = server.run();

  worker.onmessage = function(e) {
    const { type, ...event } = e;
    switch (type) {
      case 'message':
        worker.sendMessage({ type: 'send', id: e.id, msg: e.msg });
        break;
      case 'close':
        console.log('close', e);
        worker.sendMessage({ type: 'exit' });
        break;
      case 'running':
        let data;
        let filename = 'jsutils.h';

        data = fetch(filename);

        let file = std.loadFile(filename);
        console.log('data.length == file.length', data.length == file.length);
        console.log('data == file', data == file);
        assert(data, file);
        std.exit(0);
        break;
      default:
        console.log('test-server-http.onmessage', e);
        break;
    }
  };
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  std.exit(1);
}
