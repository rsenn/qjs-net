import { popen } from 'std';
import { LLL_ALL, LLL_NOTICE, LLL_USER, logLevels, createServer, setLog } from 'net';

import('console').then(({ Console }) => { globalThis.console = new Console({ inspectOptions: { compact: 0 } });
});

setLog((LLL_NOTICE - 1) | LLL_USER, (level, message) => {
  if(level == LLL_USER)
    //if(!/minnet-server-http.c/.test(message))
    console.log(
      logLevels[level].padEnd(10),
      message /*.replaceAll(/\n/g, '\\\\n')*/
        .replaceAll(/\x1b\[[^m]*m/g, '')
    );
});

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
  /* LAME has problems reading from stdin/writing to stdout, but you can try */
  //let cmd = `pacat --stream-name '${streamName}' -r --rate=44100 --format=s16le --channels=2 --raw | lame --quiet -r --alt-preset 128 - -`;

  let cmd = `sox -q -t pulseaudio '${streamName}' -r 44100 -t mp3 -`;
  let file = popen(cmd, 'r');

  let r, fd, buf;

  const waitRead = fd => new Promise((resolve, reject) => os.setReadHandler(fd, () => (os.setReadHandler(fd, null), resolve(file))));

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
    port: 8765,
    tls: true,
    protocol: 'http',
    mimetypes: [['.mp4', 'video/mp4']],
    mounts: {
      '/': ['.', 'index.html'],
      *'/404.html'(req, res) {
        yield `<html>\n\t<head>\n\t\t<meta charset=utf-8 http-equiv="Content-Language" content="en" />\n\t\t<link rel="stylesheet" type="text/css" href="/error.css" />\n\t</head>\n\t<body>\n\t\t<h1>404</h1>\n\t\tThe requested URL ${req.url.path} was not found on this server.\n\t</body>\n</html>\n`;
      },
      async *stream(req, res) {
        res.type = 'audio/mpeg';

        for await(let chunk of StreamPulseOutput()) yield chunk;
      }
    }
  })
);
