import { popen, getenv } from 'std';
import { LLL_USER, LLL_ERR, logLevels, createServer, setLog, Response } from 'net';

import('console').then(({ Console }) => (globalThis.console = new Console({ inspectOptions: { compact: 0 } })));

setLog((std.getenv('DEBUG') ? LLL_USER : 0) | LLL_ERR, (level, message) =>
  console.log(logLevels[level].padEnd(10), message.replaceAll(/\n/g, '\\\\n'))
);

let srv = createServer({
  tls: true,
  mimetypes: [['.json', 'text/json']],
  mounts: {
    '/': ['.', 'api'],
    *'/404.html'(req, resp) {
      yield `<html>\n`;
      yield `\t<head>\n`;
      yield `\t\t<meta charset=utf-8 http-equiv="Content-Language" content="en" />\n`;
      yield `\t\t<link rel="stylesheet" type="text/css" href="/error.css" />\n`;
      yield `\t</head>\n`;
      yield `\t<body><h1>404</h1>The requested URL ${req.url.path} was not found on this server.</body>\n`;
      yield `</html>\n`;
    },
    async '/api'(req) {
      console.log('/api', { req });

      let body = await req.json();
      console.log('body', body);

      return new Response('Hello, World!\n' + JSON.stringify(body, null, 2) + '\n', {
        status: 200,
        headers: { 'content-type': 'text/plain' }
      });
    }
  },
  onRequest(req, resp) {
    console.log('req:', req, 'resp:', resp);
  }
});

srv.listen(3333);

console.log(srv.onrequest);
//console.log(srv.onrequest.prev);
