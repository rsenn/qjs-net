import * as std from 'std';
import * as os from 'os';
import inspect from 'inspect';
import net, { URL } from 'net';
import { Console } from 'console';
import { Levels, DefaultLevels, Init, SetLog } from './log.js';
import Client from './client.js';

const escape = s =>
  [
    [/\r/g, '\\r'],
    [/\n/g, '\\n']
  ].reduce((a, [exp, rpl]) => a.replace(exp, rpl), s);
const abbreviate = s => (s.length > 100 ? s.substring(0, 45) + ' ... ' + s.substring(-45) : s);

const connections = new Set();

function main(...args) {
  const base = scriptArgs[0].replace(/.*\//g, '').replace(/\.[a-z]*$/, '');
  globalThis.console = new Console({ inspectOptions: { compact: 2, customInspect: true, maxStringLength: 100 } });

  const debug = args[0] == '-x' && args.shift();
  if(args.length == 0) args.push('https://github.com/rsenn?tab=repositories');

  Object.assign(globalThis, {
    get connections() {
      return [...connections];
    }
  });

  const sslCert = 'localhost.crt',
    sslPrivateKey = 'localhost.key';

  for(let arg of args) {
    Client(arg, { sslCert, sslPrivateKey }, false);
  }

  function quit(why) {
    console.log(`quit('${why}')`);
    std.exit(0);
  }
}
try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  std.exit(1);
} finally {
  //console.log('SUCCESS');
}
