import { err, exit, puts } from 'std';
import { setReadHandler, setWriteHandler } from 'os';
import { client, setLog, LLL_CLIENT, LLL_USER, URL } from 'net';
import { Init } from './log.js';
import { escape, abbreviate } from './common.js';

const connections = new Set();

export default function Client(url, options, debug) {
  //console.log('Client',{url,options,debug});
  Init('client.js', typeof debug == 'number' ? debug : LLL_CLIENT | (debug ? LLL_USER : 0));

  const { onConnect, onClose, onError, onHttp, onFd, onMessage, ...opts } = options;

  /*const sslCert = 'localhost.crt',
    sslPrivateKey = 'localhost.key';*/
 
  err.puts(`Client connecting to ${url} ...\n`);

  return client(url, {
    /*  tls: true,
    sslCert,
    sslPrivateKey,*/
    headers: {
      'User-Agent': 'minnet'
    },
    ...opts,
    onConnect(ws, req) {
      console.log('onConnect');
      connections.add(ws);
      onConnect ? onConnect(ws, req) : console.log('onConnect', ws, req);
    },
    onClose(ws, reason) {
      connections.delete(ws);

      onClose ? onClose(ws, reason) : (console.log('onClose', { ws, reason }), exit(reason != 1000 ? 1 : 0));
    },
    onError(ws, error) {
      connections.delete(ws);

      onError ? onError(ws, error) : (console.log('onError', { ws, error }), exit(error));
    },
    onHttp(req, rsp) {
      const { url, method, headers } = req;

      return onHttp ? onHttp(req, rsp) : (console.log('\x1b[38;5;82monHttp\x1b[0m', { url, method, headers }), rsp);
    },
    onFd(fd, rd, wr) {
      setReadHandler(fd, rd);
      setWriteHandler(fd, wr);
    },
    onMessage(ws, msg) {
      onMessage
        ? onMessage(ws, msg)
        : (console.log('onMessage', console.config({ maxStringLen: 100 }), { ws, msg }),
          puts(escape(abbreviate(msg)) + '\n'));
    }
  });
}

Object.defineProperty(Client, 'connections', {
  get() {
    return [...connections];
  }
});
