import { popen, getenv } from 'std';
import { LLL_USER, logLevels, createServer, setLog } from 'net';

import('console').then(({ Console }) => { globalThis.console = new Console({ inspectOptions: { compact: 0 } });
});

if(std.getenv('DEBUG'))
  setLog(LLL_USER, (level, message) => console.log(logLevels[level].padEnd(10), message.replaceAll(/\n/g, '\\\\n')));

class PulseAudio {
  static *getSources() {
    let pipe = popen(`pacmd list-sources`, 'r');
    while(!pipe.eof()) {
      let s = pipe.getline();
      if(/name:/.test(s)) yield s.slice(s.indexOf('<') + 1, -1);
    }
    pipe.close();
  }

  static async *streamSource(sourceName = sources[0], bufSize = 512) {
    /* pacat |lame has libmp3lame problems reading from stdin/writing to stdout, but you can try */
    const pipelines = {
      pacat: name =>
        `pacat --stream-name '${name}' -r --rate=44100 --format=s16le --channels=2 --raw | lame --quiet -r --alt-preset 128 - -`,
      sox: name => `sox -q -t pulseaudio '${name}' -r 44100 -t mp3 -`
    };
    const file = popen(pipelines.sox(sourceName), 'r');

    const waitRead = fd =>
      new Promise((resolve, reject) => os.setReadHandler(fd, () => (os.setReadHandler(fd, null), resolve(file))));
    const fd = file.fileno();
    const buf = new ArrayBuffer(bufSize);

    for(;;) {
      await waitRead(fd);
      let r;
      if((r = os.read(fd, buf, 0, buf.byteLength)) <= 0) break;
      yield buf.slice(0, r);
    }
    file.close();
  }
}

createServer({
  port: 8765,
  tls: true,
  mimetypes: [['.mp4', 'video/mp4']],
  mounts: {
    '/': ['.', 'index.html'],
    *'/404.html'(req, res) {
      yield `<html>\n\t<head>\n\t\t<meta charset=utf-8 http-equiv="Content-Language" content="en" />\n\t\t<link rel="stylesheet" type="text/css" href="/error.css" />\n\t</head>\n\t<body>\n\t\t<h1>404</h1>\n\t\tThe requested URL ${req.url.path} was not found on this server.\n\t</body>\n</html>\n`;
    },
    async *stream(req, resp) {
      resp.type = 'audio/mpeg';

      const [source] = [...PulseAudio.getSources()];

      yield* PulseAudio.streamSource(source);
    }
  }
});
