import { server } from 'net';

const socket = server({
  port: 3000,
  onConnect(ws) {
    console.log('New Client Connected.');
  },
  onMessage(ws, data) {
    console.log('Got Message:', data.toString());

    ws.send('Hello, MinnetServer!');
  }
});
