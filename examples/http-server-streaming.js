import { popen } from 'std';
import { LLL_ALL, LLL_NOTICE, LLL_USER, logLevels, createServer, setLog } from 'net';

import('console').then(({ Console }) => { globalThis.console = new Console({ inspectOptions: { compact: 0 } });
});

setLog((LLL_NOTICE - 1) | LLL_USER, (level, message) => console.log(logLevels[level].padEnd(10), message));

async function* StreamPulseOutput(streamName = 'alsa_output.pci-0000_00_1f.3.analog-stereo.monitor', bufSize = 4096) {
  let file = popen(
    `pacat -v --stream-name '${streamName}' -r --rate=44100 --format=s16le --channels=2 --raw | lame --quiet -r --alt-preset 128 - -`,
    'rb'
  );
  let r,
    fd = file.fileno();
  let buf = new ArrayBuffer(bufSize);

  const waitRead = fd =>
    new Promise((resolve, reject) => os.setReadHandler(fd, () => (os.setReadHandler(fd, null), resolve(file))));

  for(;;) {
    await waitRead(fd);
    if((r = os.read(fd, buf, 0, buf.byteLength)) <= 0) break;
    yield buf.slice(0, r);
  }
}

createServer(
  (globalThis.options = {
    host: '0.0.0.0',
    port: '8765',
    block: false,
    tls: true,
    protocol: 'http',
    mounts: {
      '/': ['/', '.', 'index.html'],
      '/404.html': function* (req, res) {
        console.log('/404.html', { req, res });
        yield '<html><head><meta charset=utf-8 http-equiv="Content-Language" content="en"/><link rel="stylesheet" type="text/css" href="/error.css"/></head><body><h1>403</h1></body></html>';
      },
      async *stream(req, res) {
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
