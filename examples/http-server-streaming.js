import { popen } from 'std';
import { LLL_ALL, LLL_NOTICE, LLL_USER, logLevels, createServer, setLog } from 'net';

import('console').then(({ Console }) => { globalThis.console = new Console({ inspectOptions: { compact: 0 } });
});

//setLog((LLL_NOTICE - 1) | LLL_USER, (level, message) => level == LLL_USER /*|| !/writeable/i.test(message)*/ && console.log(logLevels[level].padEnd(10), message));

function GetPulseSources() {
  let pipe = popen('pacmd list-sources', 'r');
  let sources = [];
  while(!pipe.eof()) {
    let s = pipe.getline();
    if(/name:/.test(s)) sources.push(s.slice(s.indexOf('name: <') + 7, -1));
  }
  pipe.close();
  return sources;
}
let sources = GetPulseSources();

async function* StreamPulseOutput(streamName = sources[0], bufSize = 512) {
  //& let cmd= `ffmpeg -f pulse -i '${streamName}' -codec:a libmp3lame -qscale:a 2 -`;
  //let cmd = `pacat --stream-name '${streamName}' -r --rate=44100 --format=s16le --channels=2 --raw | lame --quiet -r --alt-preset 128 - -`;

  let cmd = `sox -q -t pulseaudio '${streamName}' -t mp3 -`;
  let file = popen(cmd, 'r');

  let r, fd, buf;

  const waitRead = fd =>
    new Promise((resolve, reject) => os.setReadHandler(fd, () => (os.setReadHandler(fd, null), resolve(file))));

  fd = file.fileno();
  buf = new ArrayBuffer(bufSize);

  for(;;) {
    await waitRead(fd);
    if((r = os.read(fd, buf, 0, buf.byteLength)) <= 0) break;
    yield buf.slice(0, r);
  }
  file.close();
}

createServer(
  (globalThis.options = {
    port: '8765',
    block: false,
    tls: true,
    protocol: 'http',
    mimetypes: [['.mp4', 'video/mp4']],
    mounts: {
      '/': ['/', '.', 'index.html'],
      *'/404.html'(req, res) {
        console.log('/404.html', { req, res });
        yield '<html><head><meta charset=utf-8 http-equiv="Content-Language" content="en"/><link rel="stylesheet" type="text/css" href="/error.css"/></head><body><h1>404</h1></body></html>';
      },
      async *stream(req, res) {
        res.type = 'audio/mpeg';

        for await(let chunk of StreamPulseOutput()) yield chunk;
      }
    },
    onConnect(ws, req) {
      console.log('onConnect', { ws, req });
    },
    onClose(ws, status, reason) {
      console.log('onClose', { ws, status, reason });
      ws.close(status);
    },
    onError(ws, error) {
      console.log('onError', { ws, error });
    },
    onRequest(ws, req, resp) {
      console.log('onRequest', { req, resp });
      Object.assign(globalThis, { req, resp });
    },
    onMessage(ws, msg) {
      console.log('onMessage', ws.fd, msg);
      ws.send('ECHO: ' + msg);
    }
  })
);
