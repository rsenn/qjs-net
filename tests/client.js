import * as std from 'std';
import * as os from 'os';
import net, { URL } from 'net';
import { Levels, DefaultLevels, Init, SetLog } from './log.js';

export default function Client(url, callbacks, listen = 0) {
  const { protocol, host, port, path } = new URL(url);

  Init('CLIENT');

  console.log('createWS', { protocol, host, port, path, listen });

  const fn = [net.client, net.server][+listen];
  return fn({
    block: false,
    sslCert,
    sslPrivateKey,
    protocol,
    ssl: protocol == 'wss',
    host,
    port,
    path,
    ...callbacks,
    onConnect(ws, req) {
      console.log('onConnect', ws, req);
      connections.add(ws);
      try {
        console.log(`Connected to ${protocol}://${host}:${port}${path}`, true);
      } catch(err) {
        console.log('error:', err.message);
      }
    },
    onClose(ws, status) {
      connections.delete(ws);
      console.log('onClose', { ws, status });
      std.exit(status != 1000 ? 1 : 0);
    },
    onError(ws, error) {
      console.log('onError', { ws, error });
      std.exit(error);
    },
    onHttp(req, rsp) {
      const { url, method, headers } = req;
      console.log('\x1b[38;5;82monHttp\x1b[0m(\n\t', Object.setPrototypeOf({ url, method, headers }, Object.getPrototypeOf(req)), ',\n\t', rsp, '\n)');
      return rsp;
    },
    onFd(fd, rd, wr) {
      // console.log('onFd', fd, rd, wr);
      os.setReadHandler(fd, rd);
      os.setWriteHandler(fd, wr);
    },
    onMessage(ws, msg) {
      console.log('onMessage', console.config({ maxStringLen: 100 }), { ws, msg });

      std.puts(escape(abbreviate(msg)) + '\n');
    },
    onError(ws, error) {
      console.log('onError', ws, error);
    }
  });
}
