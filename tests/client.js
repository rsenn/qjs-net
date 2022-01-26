import * as std from 'std';
import * as os from 'os';
import net, { URL } from 'net';
import { Init, SetLog } from './log.js';

const connections = new Set();

const escape = s =>
  [
    [/\r/g, '\\r'],
    [/\n/g, '\\n']
  ].reduce((a, [exp, rpl]) => a.replace(exp, rpl), s);

const abbreviate = s => (s.length > 100 ? s.substring(0, 45) + ' ... ' + s.substring(-45) : s);

export default function Client(url, options) {
  Init('Client', net.LLL_CLIENT);

  const { onConnect, onClose, onError, onHttp, onFd, onMessage, ...opts } = options;

  return net.client(url, {
    ...opts,
    onConnect(ws, req) {
      connections.add(ws);
      onConnect ? onConnect(ws, req) : console.log('onConnect', ws, req);
    },
    onClose(ws, reason) {
      connections.delete(ws);

      onClose ? onClose(ws, reason) : (console.log('onClose', { ws, reason }), std.exit(reason != 1000 ? 1 : 0));
    },
    onError(ws, error) {
      connections.delete(ws);

      onError ? onError(ws, error) : (console.log('onError', { ws, error }), std.exit(error));
    },
    onHttp(req, rsp) {
      const { url, method, headers } = req;

      return onHttp ? onHttp(req, rsp) : (console.log('\x1b[38;5;82monHttp\x1b[0m', { url, method, headers }), rsp);
    },
    onFd(fd, rd, wr) {
      os.setReadHandler(fd, rd);
      os.setWriteHandler(fd, wr);
    },
    onMessage(ws, msg) {
      onMessage ? onMessage(ws, msg) : (console.log('onMessage', console.config({ maxStringLen: 100 }), { ws, msg }), std.puts(escape(abbreviate(msg)) + '\n'));
    }
  });
}

Object.defineProperty(Client, 'connections', {
  get() {
    return [...connections];
  }
});
