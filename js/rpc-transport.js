export function browserWSTransport(url, { WebSocket = globalThis.WebSocket } = {}) {
  const ws = new WebSocket(url);
  const t = {
    onmessage: null,
    onclose: null,
    send: msg => ws.send(msg),
    close: (code, reason) => ws.close(code, reason),
  };
  t.ready = new Promise((resolve, reject) => {
    ws.addEventListener('open', () => resolve(t));
    ws.addEventListener('error', ev => reject(new Error('websocket error')));
  });
  ws.addEventListener('message', ev => t.onmessage && t.onmessage(ev.data));
  ws.addEventListener('close', ev => t.onclose && t.onclose(ev.code, ev.reason));
  return t;
}
