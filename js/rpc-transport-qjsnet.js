import { client, generateCert } from 'net.so';
import { setReadHandler, setWriteHandler } from 'os';

let _sharedCert = null;

export function ensureCert(opts = {}) {
  if(opts.sslCert && opts.sslPrivateKey) return { sslCert: opts.sslCert, sslPrivateKey: opts.sslPrivateKey };
  if(!_sharedCert) {
    const { commonName = 'localhost', altNames = ['localhost', '127.0.0.1', '::1'], days = 365, bits = 2048 } = opts;
    _sharedCert = generateCert({ commonName, altNames, days, bits });
  }
  return { sslCert: _sharedCert.cert, sslPrivateKey: _sharedCert.key };
}

export function resetCert() {
  _sharedCert = null;
}

function makeBufferedTransport({ send, close }) {
  let buffer = [];
  let onmessage = null;
  const t = {
    send,
    close,
    onclose: null,
    onerror: null,
    get onmessage() { return onmessage; },
    set onmessage(fn) {
      onmessage = fn;
      if(fn && buffer.length) {
        const b = buffer; buffer = [];
        for(const m of b) fn(m);
      }
    },
  };
  const deliver = msg => (onmessage ? onmessage(msg) : buffer.push(msg));
  return { t, deliver };
}

export function qjsnetClientTransport(url, opts = {}) {
  let ws = null;
  const { t, deliver } = makeBufferedTransport({
    send: m => (ws ? ws.send(m) : void 0),
    close: (code = 1000, reason = 'closed') => (ws ? ws.close(code, reason) : void 0),
  });

  const certOpts = ensureCert(opts);

  t.ready = new Promise((resolve, reject) => {
    let opened = false;
    client(url, {
      block: false,
      ...certOpts,
      ...opts,
      onConnect(_ws) {
        ws = _ws;
        opened = true;
        resolve(t);
      },
      onMessage(_ws, msg) { deliver(msg); },
      onError(_ws, err) {
        if(t.onerror) t.onerror(err);
        if(!opened) reject(err);
      },
      onClose(_ws, status, reason) {
        if(t.onclose) t.onclose(status, reason);
        if(!opened) reject(new Error(`closed before open: ${status} ${reason}`));
      },
      onFd(fd, rd, wr) {
        setReadHandler(fd, rd);
        setWriteHandler(fd, wr);
      },
      onCertificateVerify(obj) { obj.ok = 2; },
    });
  });
  return t;
}

export function qjsnetServerHandlers({ onPeer, PeerCtor, peerOptions }) {
  const perFd = new Map();

  return {
    handlers: {
      onConnect(ws) {
        const { t, deliver } = makeBufferedTransport({
          send: m => ws.send(m),
          close: (code = 1000, reason = 'closed') => ws.close(code, reason),
        });
        const entry = { ws, t, deliver };
        perFd.set(ws.fd, entry);
        if(PeerCtor && onPeer) {
          const peer = new PeerCtor(t, peerOptions);
          entry.peer = peer;
          try {
            onPeer(peer, ws);
          } catch(err) {
            console.error('[qjsnetServer] onPeer threw:', err && err.message);
          }
        } else if(onPeer) {
          onPeer(t, ws);
        }
      },
      onMessage(ws, msg) {
        const entry = perFd.get(ws.fd);
        if(entry) entry.deliver(msg);
      },
      onClose(ws, status, reason) {
        const entry = perFd.get(ws.fd);
        if(!entry) return;
        if(entry.t.onclose) entry.t.onclose(status, reason);
        if(entry.peer) entry.peer.close();
        perFd.delete(ws.fd);
      },
      onError(ws) {
        const entry = perFd.get(ws.fd);
        if(!entry) return;
        if(entry.peer) entry.peer.close();
        perFd.delete(ws.fd);
      },
    },
    peers: () => [...perFd.values()].map(v => v.peer).filter(Boolean),
  };
}
