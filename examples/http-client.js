import * as os from 'os';
import net from 'net';

const cli = net.client('https://github.com/rsenn?tab=repositories', {
  block: false,
  async onResponse(ws, resp) {
    console.log('Response:', resp);
    let chunks = [];
    for await(let data of resp.body) {
      chunks.push(data);
    }

    console.log('chunks received:', chunks.length);
    console.log(
      'bytes received:',
      chunks.reduce((total, chunk) => total + chunk.length, 0)
    );
  },
  onClose(ws, reason) {
    console.log('closed',ws.close);
   // ws.close();
  },
  onFd(fd, rd, wr) {
    console.log('onFd', fd, rd, wr);
    os.setReadHandler(fd, rd);
    os.setWriteHandler(fd, wr);
  }
});
