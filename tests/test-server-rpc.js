import { kill, SIGTERM, sleep, setTimeout, WNOHANG } from 'os';
import { RPCPeer } from '../js/rpc-peer.js';
import { qjsnetClientTransport } from '../js/rpc-transport-qjsnet.js';
import { Init, log } from './log.js';
import { spawn, wait4 } from './spawn.js';
import { exit } from 'std';

class SessionRecorder {
  constructor() {
    this.sessionId = null;
    this.events = [];
    this.domSnapshots = [];
  }

  start({ url, at }) {
    this.sessionId = `sess-${Math.random().toString(36).slice(2, 10)}`;
    log(`[browser] recorder.start url=${url} at=${at} -> ${this.sessionId}`);
    setTimeout(() => this._fakeActivity(), 30);
    return this.sessionId;
  }

  log(msg) {
    log(`[browser] recorder.log ${msg}`);
    this.events.push({ t: Date.now(), kind: 'log', msg });
    return true;
  }

  captureDom() {
    const snap = '<html><body><h1>demo</h1><p>captured at ' + Date.now() + '</p></body></html>';
    this.domSnapshots.push(snap);
    return snap;
  }

  stop() {
    log(`[browser] recorder.stop events=${this.events.length} snapshots=${this.domSnapshots.length}`);
    return { events: this.events, snapshots: this.domSnapshots };
  }

  _fakeActivity() {
    this.events.push({ t: Date.now(), kind: 'click', target: '#login' });
    this.events.push({ t: Date.now() + 1, kind: 'input', target: '#user', value: 'alice' });
  }
}

async function orchestrateAsBrowser(url) {
  log(`browser connecting to ${url}`);
  const transport = qjsnetClientTransport(url, { tls: true });
  await transport.ready;
  log('browser connected');

  const peer = new RPCPeer(transport, { verbose: false });
  const recorder = new SessionRecorder();
  peer.expose('recorder', recorder);

  const scanner = peer.remote('scanner');

  const ping = await scanner.echo('hello');
  log(`browser: scanner.echo -> ${ping}`);

  const targetPort = +(url.match(/:(\d+)/) || [])[1] || 30000;
  const ports = [22, 80, 443, 8080, targetPort, targetPort + 1];
  log(`browser: scanning 127.0.0.1 ports=${ports.join(',')}`);
  const results = await scanner.scan('127.0.0.1', ports, { timeoutMs: 250 });
  for(const r of results) log(`  ${r.host}:${r.port} ${r.state}`);

  const open = results.filter(r => r.state === 'open').map(r => r.port);
  const expectedOpen = open.includes(targetPort);
  log(`browser: server port ${targetPort} detected open? ${expectedOpen}`);

  await sleep(80);
  const state = recorder.stop();
  log(`browser: recorded ${state.events.length} events`);

  peer.close();
  return expectedOpen && state.events.length > 0;
}

async function main() {
  Init(scriptArgs[0]);
  const port = 30000;
  const logFile = scriptArgs[0].replace(/.*\//g, '').replace('.js', '.log');
  const pid = spawn('server-rpc.js', ['localhost', String(port)], logFile);
  log(`spawned server-rpc pid=${pid}`);
  sleep(300);

  let ok = false;
  try {
    ok = await orchestrateAsBrowser(`wss://localhost:${port}/ws`);
  } catch(err) {
    log(`FAIL: ${err && err.message}\n${err && err.stack}`);
  }

  try { kill(pid, SIGTERM); } catch {}
  const status = [];
  wait4(pid, status, WNOHANG);
  log(`server status=${status[0]}`);

  exit(ok ? 0 : 1);
}

main().catch(err => {
  log(`FAIL: ${err && err.message}\n${err && err.stack}`);
  exit(1);
});
