const isFn = v => typeof v === 'function';
const isThenable = v => v != null && typeof v === 'object' && isFn(v.then);

export const JSONCodec = {
  name: 'json',
  encode: v => JSON.stringify(v),
  decode: v => JSON.parse(v),
};

export class RPCError extends Error {
  constructor(message, code, data) {
    super(message);
    this.code = code;
    if(data !== undefined) this.data = data;
  }
}

export class RPCPeer {
  constructor(transport, { codec = JSONCodec, verbose = false } = {}) {
    this.transport = transport;
    this.codec = codec;
    this.verbose = verbose;
    this.exposed = new Map();
    this.pending = new Map();
    this.nextId = 1;
    this.closed = false;

    transport.onmessage = raw => this._recv(raw);
    const prevClose = transport.onclose;
    transport.onclose = (code, reason) => {
      this._teardown(`transport closed: ${code ?? ''} ${reason ?? ''}`.trim());
      if(prevClose) prevClose(code, reason);
    };
  }

  expose(name, instance) {
    this.exposed.set(name, instance);
    return this;
  }

  unexpose(name) {
    this.exposed.delete(name);
    return this;
  }

  remote(name) {
    const call = (method, params) => this.call(`${name}.${method}`, params);
    const notify = (method, params) => this.notify(`${name}.${method}`, params);
    return new Proxy(
      { [Symbol.for('rpc-remote')]: name },
      {
        get: (target, prop) => {
          if(prop === 'then') return undefined;
          if(prop in target) return target[prop];
          if(typeof prop !== 'string') return undefined;
          return (...args) => call(prop, args);
        },
      },
    );
  }

  call(method, params = []) {
    if(this.closed) return Promise.reject(new RPCError('peer closed', -32000));
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this._send({ id, method, params });
    });
  }

  notify(method, params = []) {
    if(this.closed) return;
    this._send({ method, params });
  }

  close() {
    this._teardown('closed by peer');
    if(this.transport.close) try { this.transport.close(); } catch {}
  }

  _teardown(reason) {
    if(this.closed) return;
    this.closed = true;
    const err = new RPCError(reason, -32000);
    for(const { reject } of this.pending.values()) reject(err);
    this.pending.clear();
  }

  async _recv(raw) {
    if(raw == null || raw === '') return;
    let msg;
    try {
      msg = this.codec.decode(raw);
    } catch(e) {
      this._log('decode error:', e && e.message);
      return;
    }

    if(msg && 'method' in msg) return this._handleRequest(msg);
    if(msg && 'id' in msg) return this._handleResponse(msg);
  }

  async _handleRequest(msg) {
    const { id, method, params = [] } = msg;
    const dot = method.indexOf('.');
    if(dot < 0) {
      if(id != null) this._send({ id, error: { code: -32600, message: `invalid method path: ${method}` } });
      return;
    }
    const objName = method.slice(0, dot);
    const propName = method.slice(dot + 1);
    const obj = this.exposed.get(objName);
    if(!obj || !(propName in obj) || !isFn(obj[propName])) {
      if(id != null) this._send({ id, error: { code: -32601, message: `no such method: ${method}` } });
      return;
    }
    try {
      let result = obj[propName].apply(obj, params);
      if(isThenable(result)) result = await result;
      if(id != null) this._send({ id, result });
    } catch(err) {
      this._log('handler threw:', err && err.message);
      if(id != null)
        this._send({
          id,
          error: {
            code: err.code ?? -32000,
            message: err.message ?? String(err),
            data: err.stack,
          },
        });
    }
  }

  _handleResponse(msg) {
    const p = this.pending.get(msg.id);
    if(!p) return;
    this.pending.delete(msg.id);
    if('error' in msg) {
      const e = msg.error || {};
      p.reject(new RPCError(e.message ?? 'RPC error', e.code, e.data));
    } else {
      p.resolve(msg.result);
    }
  }

  _send(obj) {
    this._log('->', obj);
    try {
      this.transport.send(this.codec.encode(obj));
    } catch(err) {
      this._log('send error:', err && err.message);
    }
  }

  _log(...args) {
    if(this.verbose) console.log('[RPCPeer]', ...args);
  }
}
