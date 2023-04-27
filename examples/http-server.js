import { LLL_ALL, LLL_NOTICE, LLL_USER, logLevels, createServer, setLog } from 'net';

import('console').then(({ Console }) => { globalThis.console = new Console({ inspectOptions: { compact: 0 } });
});

setLog(/*LLL_ALL |*/ (LLL_NOTICE - 1) | LLL_USER, (level, message) => console.log(logLevels[level].padEnd(10), message));

createServer(
  (globalThis.options = {
    port: 8765,
     tls: true,
    protocol: 'http',
    mounts: {
      '/': ['/', '.', 'index.html'],
      *'/404.html' (req, res) {
         yield '<html><head><meta charset=utf-8 http-equiv="Content-Language" content="en"/><link rel="stylesheet" type="text/css" href="/error.css"/></head><body><h1>403</h1></body></html>';
      },
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
    },
 
    onRequest(ws, req, resp) {
      console.log('onRequest', { req, resp });
      Object.assign(globalThis, { req, resp });
    } 
  })
);
