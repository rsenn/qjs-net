import { client } from 'net.so';
import { createServer } from 'net.so';
import { LLL_INFO } from 'net.so';
import { LLL_USER } from 'net.so';
import { LLL_WARN } from 'net.so';
import { logLevels } from 'net.so';
import { setLog } from 'net.so';
import { setReadHandler } from 'os';
import { setTimeout } from 'os';
import { setWriteHandler } from 'os';
import { clearTimeout } from 'os';
import { RPCPeer } from '../js/rpc-peer.js';
import { ensureCert, qjsnetServerHandlers } from '../js/rpc-transport-qjsnet.js';
import { Init, log } from './log.js';
import { exit, getenv } from 'std';

class PortScanner {
  constructor(peer) {
    this.peer = peer;
    this.active = new Set();
  }

  async probe(host, port, timeoutMs = 400) {
    return new Promise(resolve => {
      let done = false,
        conn,
        timer;
      const finish = state => {
        if(done) return;
        done = true;
        this.active.delete(conn);
        if(timer) clearTimeout(timer);
        if(conn && conn.close) try { conn.close(); } catch {}
        resolve({ host, port, state });
      };
      try {
        conn = client(`raw://${host}:${port}`, {
          block: false,
          tls: false,
          onConnect() { finish('open'); },
          onError() { finish('closed'); },
          onClose() { if(!done) finish('closed'); },
          onFd(fd, rd, wr) {
            setReadHandler(fd, rd);
            setWriteHandler(fd, wr);
          },
        });
        if(conn) this.active.add(conn);
      } catch(err) {
        return finish('error');
      }
      timer = setTimeout(() => finish('filtered'), timeoutMs);
    });
  }

  async scan(host, ports, { concurrency = 16, timeoutMs = 400 } = {}) {
    log(`scan ${host} ${ports.length} ports (concurrency=${concurrency})`);
    const results = [];
    let idx = 0;
    const workers = Array.from({ length: Math.min(concurrency, ports.length) }, async () => {
      while(idx < ports.length) {
        const p = ports[idx++];
        const r = await this.probe(host, p, timeoutMs);
        results.push(r);
        if(r.state === 'open') {
          try { await this.peer.remote('recorder').log(`OPEN ${host}:${p}`); } catch {}
        }
      }
    });
    await Promise.all(workers);
    return results.sort((a, b) => a.port - b.port);
  }

  echo(x) { return x; }
}

function Server(scriptPath, host = 'localhost', port = 30000) {
  const mydir = scriptPath.replace(/(\/|^)[^\/]*$/g, '$1.').replace(/\/\.$/g, '');
  const parentDir = mydir + '/..';

  const { sslCert, sslPrivateKey } = ensureCert({ commonName: host, altNames: [host, 'localhost', '127.0.0.1'] });
  log(`using in-memory self-signed cert (CN=${host})`);

  setLog(LLL_WARN | LLL_USER | (getenv('DEBUG') ? LLL_INFO : 0), (level, message) => !/LOAD_EXTRA|VHOST_CERT_AGING|EVENT_WAIT/.test(message) && log(`lws:${logLevels[level].padEnd(6)} ${message.trim()}`));

  const rpc = qjsnetServerHandlers({
    PeerCtor: RPCPeer,
    peerOptions: { verbose: !!getenv('RPC_VERBOSE') },
    onPeer(peer, ws) {
      log(`peer connected fd=${ws.fd}`);
      const scanner = new PortScanner(peer);
      peer.expose('scanner', scanner);

      Promise.resolve()
        .then(() => peer.remote('recorder').start({ url: 'http://example.local/', at: Date.now() }))
        .then(sessionId => log(`recorder.start -> ${JSON.stringify(sessionId)}`))
        .catch(err => log(`recorder.start FAILED: ${err && err.message}`));
    },
  });

  return createServer({
    host,
    port,
    protocol: 'http',
    tls: true,
    block: false,
    sslCert,
    sslPrivateKey,
    mounts: { '/': [parentDir, 'index.html'] },
    onConnect: rpc.handlers.onConnect,
    onMessage: rpc.handlers.onMessage,
    onClose: rpc.handlers.onClose,
    onError: rpc.handlers.onError,
    onCertificateVerify(obj) { obj.ok = 2; },
  });
}

function main() {
  Init(scriptArgs[0]);
  try {
    const [, host = 'localhost', portStr = '30000'] = scriptArgs;
    const srv = Server(scriptArgs[0], host, +portStr);
    log(`server-rpc listening on ${host}:${portStr}`);
    globalThis.server = srv;
  } catch(err) {
    log(`server-rpc failed: ${err && err.message}\n${err && err.stack}`);
    exit(1);
  }
}

if(/server-rpc\.js$/.test(scriptArgs[0])) main();
