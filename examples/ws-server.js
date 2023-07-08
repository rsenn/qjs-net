import { createServer } from 'net';
import { LLL_ERR } from 'net';
import { LLL_USER } from 'net';
import { setLog } from 'net';
setLog((std.getenv('DEBUG') ? LLL_USER : 0) | LLL_ERR, (level, message) =>
  console.log(logLevels[level].padEnd(10), message.replaceAll(/\n/g, '\\\\n'))
);

const socket = createServer({
  port: 3000,
  onConnect(ws) {
    console.log('New Client Connected.');
  },
  onMessage(ws, data) {
    console.log('Got Message:', data.toString());

    ws.send('Hello, MinnetServer!');
  }
});