import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect';
import { ReadableStream, WritableStream } from 'stream';
import Console from '../lib/console.js';

('use strict');
('use math');

function WriteFile(file, data) {
  let f = std.open(file, 'w+');
  f.puts(data);
  console.log('Wrote "' + file + '": ' + data.length + ' bytes');
}

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      colors: true,
      depth: 2,
      maxArrayLength: 4,
      maxStringLength: 60,
      compact: 1
    }
  });

  std.gc();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
