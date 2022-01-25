import * as std from 'std';
import * as os from 'os';
import inspect from 'inspect';
import net, { URL } from 'net';
import { Console } from 'console';
import { MinnetServer } from './server.js';

function main(...args) {
  globalThis.console = new Console({ inspectOptions: { compact: 2, customInspect: true, maxStringLength: 100 } });

  const sslCert = 'localhost.crt',
    sslPrivateKey = 'localhost.key';

  let [pid] = os.readlink('/proc/self');

  console.log('pid', pid);
  let server = new MinnetServer({ protocol: 'http', sslCert, sslPrivateKey });

  let worker=server.run();

  worker.onmessage = function(e) {
    console.log('client_http.onmessage', e);
  }
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  std.exit(1);
}
