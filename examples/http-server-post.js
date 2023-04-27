import { popen, getenv } from 'std';
import { LLL_USER, logLevels, createServer, setLog } from 'net';

import('console').then(({ Console }) => { globalThis.console = new Console({ inspectOptions: { compact: 0 } });
});

 if(std.getenv('DEBUG'))  setLog(LLL_USER, (level, message) => console.log(logLevels[level].padEnd(10), message.replaceAll(/\n/g, '\\\\n')));

let srv = createServer({
  tls: true,
  mimetypes: [['.mp3', 'audio/mpeg']],
  mounts: {
    '/': ['.', 'stream.mp3'],
    *'/404.html'(req, resp) {
      yield `<html>\n\t<head>\n\t\t<meta charset=utf-8 http-equiv="Content-Language" content="en" />\n\t\t<link rel="stylesheet" type="text/css" href="/error.css" />\n\t</head>\n\t<body>\n\t\t<h1>404</h1>\n\t\tThe requested URL ${req.url.path} was not found on this server.\n\t</body>\n</html>\n`;
    }
  },
  onRequest(req, resp) {
    console.log('req:', req, 'resp:', resp);
  }
});

srv.listen(3333);
srv.post('/api', async (req, resp) => {
  console.log(req.url + '', req, resp);
  let json = await req.json();
  console.log('json', json);
});

console.log(srv.onrequest);
//console.log(srv.onrequest.prev);
