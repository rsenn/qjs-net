import { EventEmitter } from './event-emitter.js';

let sockId;

export const LogWrap = (globalThis.LogWrap = function LogWrap(log) {
  if(typeof log == 'string') {
    let str = log;
    log = (...args) => console.log(str, ...args);
  } else if(!log) {
    log = (...args) => console.log(...args);
  }
  return (value, ...args) => (log(value, ...args), value);
});

/** @interface MessageReceiver */
export class MessageReceiver extends EventEmitter {
  static [Symbol.hasInstance](instance) {
    return 'onmessage' in instance;
  }

  /** @abstract */
  onmessage(msg) {
    throw new Error(`MessageReceiver.onmessage unimplemented`);
  }
}

/** @interface MessageTransmitter */
export class MessageTransmitter {
  static [Symbol.hasInstance](instance) {
    return typeof sendMessage == 'function';
  }
  /** @abstract */
  sendMessage() {
    throw new Error(`MessageReceiver.sendMessage unimplemented`);
  }
}

/**
 * @interface MessageTransceiver
 * @mixes MessageReceiver
 * @mixes MessageTransmitter
 */
export function MessageTransceiver() {}

define(MessageTransceiver.prototype, MessageReceiver.prototype, MessageTransmitter.prototype);

Object.defineProperty(MessageTransceiver, Symbol.hasInstance, {
  value: instance => [MessageReceiver, MessageTransmitter].every(ctor => ctor[Symbol.hasInstance](instance))
});

const codecs = {
  none() {
    return {
      name: 'none',
      encode: v => v,
      decode: v => v
    };
  },
  json(verbose = false) {
    return {
      name: 'json',
      encode: v => JSON.stringify(v, ...(verbose ? [null, 2] : [])),
      decode: v => JSON.parse(v)
    };
  }
};

import('inspect') .then(m => (globalThis.inspect = m)) .catch(() => {});

if(globalThis.inspect) {
  codecs.js = function js(verbose = false) {
    return {
      name: 'js',
      encode: v => inspect(v, { colors: false, compact: verbose ? false : -2 }),
      decode: v => eval(`(${v})`)
    };
  };
}

import('bjson') .then(m => (globalThis.bjson = m)) .catch(() => {});

if(globalThis.bjson) {
  codecs.bjson = function bjson() {
    return {
      name: 'bjson',
      encode: v => bjson.write(v),
      decode: v => bjson.read(v)
    };
  };
}

export function RPCApi(c) {
  let api;
  api = define(new.target ? this : new RPCApi(c), { connection: c });

  return api;
}

for(let cmd of ['list', 'new', 'methods', 'properties', 'keys', 'names', 'symbols', 'call', 'set', 'get'])
  RPCApi.prototype[cmd] = MakeCommandFunction(cmd, o => o.connection);

export function RPCProxy(c) {
  let obj = define(new.target ? this : new RPCProxy(c), { connection: c });

  return new Proxy(obj, {});
}

export function RPCObject(id, connection) {
  let obj = define(new.target ? this : new RPCObject(id), { connection, id });
  return api.methods({ id }).then(r => Object.assign(obj, r));
}

RPCObject.prototype[Symbol.toStringTag] = 'RPCObject';

export function RPCFactory(api) {
  async function Factory(opts) {
    if(typeof opts == 'string') {
      const name = opts;
      opts = { class: name };
    }
    let instance = await api.new(opts);
    let { connection } = api;
    let obj = Object.setPrototypeOf({ instance, connection }, RPCObject.prototype);

    define(obj, await api.methods(instance));
    return obj;
  }

  return Factory;
}

RPCFactory.prototype = function() {};
RPCFactory.prototype[Symbol.toStringTag] = 'RPCFactory';

/**
 * @interface Connection
 */
export class Connection extends EventEmitter {
  static fromSocket = new WeakMap();

  lastSeq = 0;

  static get last() {
    return this.list.last;
  }

  makeSeq() {
    return ++this.lastSeq;
  }

  constructor(log = (...args) => console.log(...args), codec = 'none') {
    super();

    define(this, {
      seq: 0,
      exception: null,
      log,
      messages: { requests: {}, responses: {} }
    });

    define(this, typeof codec == 'string' && codecs[codec] ? { codecName: codec, codec: codecs[codec]() } : {});
    define(this, typeof codec == 'object' && codec.name ? { codecName: codec.name, codec } : {});

    (this.constructor ?? Connection).set.add(this);
  }

  error(message) {
    console.log(`ERROR: ${message}`);
    this.exception = new Error(message);
    console.log('error(', status, message, ')');
    this.close(status, message.slice(0, 128));
    return this.exception;
  }

  close(status, reason) {
    console.log('close(', status, reason, ')');
    socket.close(status, reason);
    delete this.fd;
    this.connected = false;
  }

  onmessage(msg) {
    let { codec, codecName } = this;

    if(!msg) return;

    if(typeof msg == 'string' && msg.trim() == '') return;

    //this.log('Connection.onmessage', { msg, codec, codecName });

    let data;

    try {
      data = codec.decode((msg && msg.data) || msg);
    } catch(err) {
      throw this.error(`${this.codec.name} parse error: ${(err && err.message) || msg}`);
      return this.exception;
    }
    let response = this.processMessage(data);

    //this.log('Connection.onmessage', { data, response });
    console.log('onmessage(1)', { data });

    if(this.sendMessage) {
      if(isThenable(response)) response.then(r => this.sendMessage(r));
      else if(response !== undefined) this.sendMessage(response);
    }

    return response;
  }

  processMessage(data) {
    this.log('Connection', '.processMessage', { data });
    throw new Error('Virtual method');
  }

  onconnect(sock) {}

  onopen = LogWrap('Connection.onopen');

  onpong(data) {
    console.log('Connection.onpong:', data);
  }

  onerror(error) {
    console.log('Connection.onerror', error ? ` (${error})` : '');
    this.connected = false;
    this.cleanup();
  }

  onclose(code, why) {
    console.log('Connection.onclose', code, why ? ` (${why})` : '');
    this.connected = false;
    this.cleanup();
  }

  cleanup() {
    if(this.instances) for(let id in this.instances) delete this.instances[id];
  }

  sendMessage(obj) {
    console.log('Connection.sendMessage', obj);
    if(typeof obj == 'object')
      if(typeof obj.seq == 'number') {
        if(this.messages && this.messages.requests) this.messages.requests[obj.seq] = obj;
      } else {
        obj.seq = this.makeSeq();
      }
    let msg = typeof obj != 'string' ? this.codec.encode(obj) : obj;

    if(this.send) return this.send(msg);

    if(this.socket) return this.socket.send(msg);
  }

  sendCommand(command, params = {}) {
    let message = { command, ...params };

    console.log('Connection.sendCommand', { command, params, message });

    if(typeof params == 'object' && params != null && typeof params.seq != 'number')
      params.seq = this.seq = (this.seq | 0) + 1;

    if(this.messages && this.messages.requests)
      if(typeof params.seq == 'number') this.messages.requests[params.seq] = message;

    if(this.messages && this.messages.requests) this.messages.requests[params.seq] = message;

    return this.sendMessage(message);
  }

  static getCallbacks(instance, verbosity = 0) {
    const { classes, fdlist, log } = instance;
    const ctor = this;
    const verbose = verbosity >= 1 ? (msg, ...args) => log('VERBOSE ' + msg, args) : () => {};

    const handle = (sock, event, ...args) => {
      let conn, obj;

      if((conn = fdlist[sock.fd])) {
        callHandler(conn, event, ...args);
      } else {
        throw new Error(`No connection for fd #${sock.fd}!`);
      }
      obj = { then: fn => (fn(sock.fd), obj) };
      return obj;
    };
    const remove = sock => {
      const { fd } = sock;
      delete fdlist[fd];
    };
    return {
      verbosity,
      onConnect(sock, req) {
        verbose(`Connected`, sock);
        let connection = fdlist[sock.fd];
        if(!connection) connection = new ctor(sock, instance, log, 'json', classes);
        connection.socket ??= sock;
        const { url, method, headers } = req;
        verbose(`Connected`, sock, req);
        fdlist[sock.fd] = connection;
        handle(sock, 'connect', sock, req);
      },
      onOpen(sock) {
        verbose(`Opened`, sock, ctor.name);
        fdlist[sock.fd] = new ctor(sock, instance, log, 'json', classes);
        fdlist[sock.fd].socket ??= sock;
        handle(sock, 'open');
      },
      onMessage(sock, msg) {
        verbose(`Message`, sock, msg);
        handle(sock, 'message', msg);
      },
      onError(sock, error) {
        verbose(`Error`, sock, error);
        callHandler(instance, 'error', error);
        handle(sock, 'error', error);
        remove(sock);
      },
      onClose(sock, code, why) {
        verbose(`Closed`, sock, code, why);
        handle(sock, 'close', code, why);
        remove(sock);
      },
      onPong(sock, data) {
        verbose(`Pong`, sock, data);
        handle(sock, 'pong', data);
      }
    };
  }
}

weakDefine(Connection.prototype, { [Symbol.toStringTag]: 'Connection' });

Connection.list = [];

function RPCServerEndpoint(classes = {}, log = console.log) {
  return {
    new({ class: name, args = [] }) {
      log('RPCServerEndpoint.new');

      let obj, ret, id;
      try {
        obj = new classes[name](...args);
        id = this.makeId();
        this.instances[id] = obj;
      } catch(e) {
        return statusResponse(false, e.message + '\n' + e.stack);
      }
      return { success: true, result: { id, name } };
    },
    list() {
      log('RPCServerEndpoint.list');

      return { success: true, result: Object.keys({ ...classes, ...classes }) };
    },
    delete: objectCommand(({ id }, respond) => {
      delete this.instances[id];
      return respond(true);
    }),
    call: objectCommand(({ obj, method, params = [], id }, respond) => {
      if(method in obj && typeof obj[method] == 'function') {
        const result = obj[method](...params);
        if(isThenable(result))
          return result.then(result => respond(true, result)).catch(error => respond(false, error));
        return respond(true, result);
      }
      return respond(false, `No such method on object #${id}: ${method}`);
    }),
    keys: objectCommand(({ obj, enumerable = true }, respond) =>
      respond(true, GetProperties(obj, enumerable ? obj => Object.keys(obj) : obj => Object.getOwnPropertyNames(obj)))
    ),
    names: objectCommand(({ obj, enumerable = true }, respond) =>
      respond(
        true,
        GetProperties(obj, obj => Object.getOwnPropertyNames(obj))
      )
    ),
    symbols: objectCommand(({ obj, enumerable = true }, respond) =>
      respond(
        true,
        GetProperties(obj, obj => Object.getOwnPropertySymbols(obj)).map(sym => sym.description)
      )
    ),
    properties: MakeListCommand(v => typeof v != 'function'),
    methods: MakeListCommand(v => typeof v == 'function', { enumerable: false }),
    get: objectCommand(({ obj, property, id }, respond) => {
      if(property in obj && typeof obj[property] != 'function') {
        const result = obj[property];
        return respond(true, SerializeValue(result));
      }
      return respond(false, `No such property on object #${id}: ${property}`);
    }),
    set: objectCommand(({ obj, property, value }, respond) => respond(true, (obj[property] = value)))
  };
}

export class RPCServer {
  #commands = null;
  #lastId = 0;

  constructor(log = console.log, codec = codecs.json(false), classes = {}) {
    define(this, {
      log,
      codec,
      instances: {},
      messages: { requests: {}, responses: {} }
    });

    this.#commands = RPCServerEndpoint(classes);

    RPCServer.set.add(this);

    this.log('RPCServer.constructor', { classes, codec, log });
  }

  makeId() {
    return ++this.#lastId;
  }

  commandFunction(cmd) {
    let fn = this.#commands[cmd];

    return (...args) => fn.call(this, ...args);
  }

  processMessage(data) {
    let ret = null;
    /* prettier-ignore */ this.log('\x1b[1;31m' + this[Symbol.toStringTag], '\x1b[1;33mprocessMessage(1)\x1b[0m', console.config({ compact: 1 }), { data });
    if(!('command' in data)) return statusResponse(false, `No command specified`);
    const { command, seq, params } = data;
    let fn = this.commandFunction(command);
    /* prettier-ignore */ this.log('\x1b[1;31m' + this[Symbol.toStringTag], '\x1b[1;33mprocessMessage(2)\x1b[0m', console.config({ compact: 1 }), { command, seq, fn, params  });
    if(typeof seq == 'number') this.messages.requests[seq] = data;
    if(typeof fn == 'function') ret = fn.call(this, data);
    /*switch (command) {
      default: {
        ret = statusResponse(false, `No such command '${command}'`);
        break;
      }
    }*/
    /* prettier-ignore */ this.log('\x1b[1;31m' + this[Symbol.toStringTag], '\x1b[1;33mprocessMessage(3)\x1b[0m', console.config({ compact: 2 }),ret);
    return ret;
  }
}

define(RPCServer.prototype, { [Symbol.toStringTag]: 'RPCServer' });

RPCServer.list = [];

/**
 * @class This class describes a client connection.
 *
 * @class      RPCClient
 * @param      {Object} socket
 * @param      {Object} classes
 * @param      {Object} instance
 * @param      {Function} instance
 *
 */
export class RPCClient extends Connection {
  constructor(log = console.log, codec = codecs.json(false), classes = {}) {
    super(log, codec);

    //define(this, {log});
    //
    this.instances = {};

    RPCClient.set.add(this);

    console.log('RPCClient.on', this.on);

    let api;

    Object.defineProperties(this, {
      api: {
        get() {
          return api ?? (api = new RPCApi(this));
        }
      }
    });
  }

  processMessage(response) {
    const { success, error, result, seq } = response;

    this.log('RPCClient.processMessage', { success, error, result, seq });

    if(success) this.emit('response', result);
    else if(error) this.emit('error', error);
  }

  command(name, params) {
    return new Promise((accept, reject) => {
      this.once('response', response => accept(response));
      this.once('error', e => reject(e));

      this.sendCommand(name, params);
    });
  }
}

define(RPCClient.prototype, { [Symbol.toStringTag]: 'RPCClient' });

/**
 * @class Creates new RPC socket
 *
 * @param      {string}     [url=window.location.href]     URL (ws://127.0.0.1) or Port
 * @param      {function}   [service=RPCServer]  The service constructor
 * @return     {RPCSocket}  The RPC socket.
 */
export function RPCSocket(url, service = RPCServer, verbosity = 1) {
  if(!new.target) return new RPCSocket(url, service, verbosity);

  console.log('RPCSocket', { url, service, verbosity });

  const instance = new.target ? this : new RPCSocket(url, service, verbosity);
  const log = (...args) => console.log(...args);

  define(instance, {
    get fd() {
      let ret = Object.keys(this.fdlist)[0] ?? -1;
      if(!isNaN(+ret)) ret = +ret;
      return ret;
    },
    get socket() {
      return this.fdlist[this.fd]?.socket;
    },
    get connection() {
      return this.fdlist[this.fd];
    },
    fdlist: {},
    classes: {},
    log
  });
  console.log('RPCSocket', service);

  const callbacks = Connection.getCallbacks(instance, verbosity);

  if(!url) url = globalThis.location?.href;
  if(typeof url != 'object') url = parseURL(url);

  define(instance, {
    service,
    callbacks,
    url,
    log,
    register(ctor) {
      if(typeof ctor == 'object' && ctor !== null) {
        for(let name in ctor) instance.classes[name] = ctor[name];
      } else {
        instance.classes[ctor.name] = ctor;
      }
      return this;
    },
    listen(new_ws = MakeWebSocket, os = globalThis.os) {
      this.log(`${service.name} listening on ${this.url}`);
      if(os) callbacks.onFd = setHandlersFunction(os);
      this.listening = true;
      this.ws = new_ws(this.url, callbacks, true);
      if(new_ws !== MakeWebSocket)
        if(this.ws.then) this.ws.then(() => (this.listening = false));
        else this.listening = false;
      return this;
    },
    connect(new_ws = MakeWebSocket, os = globalThis.os) {
      this.log(`${service.name} connecting to ${this.url}`);
      if(os) callbacks.onFd = setHandlersFunction(os);
      this.ws = new_ws(this.url, callbacks, false);

      return this;
    },
    get connected() {
      const ws = this.ws;
      console.log('ws', ws);
      if(ws) return typeof ws.readyState == 'number' ? ws.readyState == ws.OPEN : false;
      const { fdlist } = instance;
      console.log('fdlist', fdlist);

      return fdlist[Object.keys(fdlist)[0]].connected;
    }
  });

  RPCSocket.set.add(instance);

  return instance;
}

for(let ctor of [RPCSocket, Connection, RPCClient, RPCServer]) {
  let set = new Set();
  define(ctor, {
    set,
    get list() {
      return [...set];
    },
    get last() {
      return this.list.last;
    }
  });
}

Object.defineProperty(RPCSocket.prototype, Symbol.toStringTag, { value: 'RPCSocket' });

function MakeWebSocket(url, callbacks) {
  let ws;
  try {
    ws = new WebSocket(url + '');
  } catch(error) {
    callbacks.onError(ws, error);
    return null;
  }
  ws.onconnect = () => callbacks.onConnect(ws);
  ws.onopen = () => callbacks.onOpen(ws);
  ws.onerror = error => callbacks.onError(ws, error);
  ws.onmessage = msg => callbacks.onMessage(ws, msg);
  ws.onpong = pong => callbacks.onPong(ws, pong);
  ws.onclose = reason => callbacks.onClose(ws, reason);
  ws.fd = sockId = (sockId | 0) + 1;

  return ws;
}

export function isThenable(value) {
  return typeof value == 'object' && value != null && typeof value.then == 'function';
}

export function hasHandler(obj, eventName) {
  if(typeof obj == 'object' && obj != null) {
    const handler = obj['on' + eventName];
    if(typeof handler == 'function') return handler;
  }
}

export function callHandler(obj, eventName, ...args) {
  let ret,
    fn = hasHandler(obj, eventName);
  if(fn) return fn.call(obj, ...args);
}

export function parseURL(url_or_port) {
  let protocol, host, port;
  if(!isNaN(+url_or_port)) [protocol, host, port] = ['ws', '0.0.0.0', url_or_port];
  else {
    [protocol = 'ws', host, port = 80] = [.../(.*:\/\/|)([^:/]*)(:[0-9]+|).*/.exec(url_or_port)].slice(1);
    if(typeof port == 'string') port = port.slice(1);
  }
  port = +port;
  if(protocol) {
    protocol = protocol.slice(0, -3);
    if(protocol.startsWith('http')) protocol = protocol.replace('http', 'ws');
  } else {
    protocol = 'ws';
  }

  return define(
    {
      protocol,
      host,
      port
    },
    {
      toString() {
        const { protocol, host, port } = this;
        return `${protocol || 'ws'}://${host}:${port}`;
      }
    }
  );
}

export function GetProperties(
  obj,
  method = obj => Object.getOwnPropertyNames(obj),
  pred = (obj, depth) => obj !== Object.prototype
) {
  let set = new Set();
  let depth = 0;
  do {
    if(!pred(obj, depth)) break;
    for(let prop of method(obj, depth)) set.add(prop);
    let proto = Object.getPrototypeOf(obj);
    if(proto === obj) break;
    obj = proto;
    ++depth;
  } while(typeof obj == 'object' && obj != null);
  return [...set];
}

export function GetKeys(obj, pred = (obj, depth) => obj !== Object.prototype) {
  let keys = new Set();
  for(let key of GetProperties(obj, obj => Object.getOwnPropertyNames(obj), pred)) keys.add(key);
  for(let key of GetProperties(obj, obj => Object.getOwnPropertySymbols(obj), pred)) keys.add(key);
  return [...keys];
}

export function getPropertyDescriptors(obj, merge = true, pred = (proto, depth) => true) {
  let a = [];
  let depth = 0,
    desc,
    ok;
  do {
    desc = Object.getOwnPropertyDescriptors(obj);
    try {
      ok = pred(obj, depth);
    } catch(e) {}
    if(ok) a.push(desc);
    let proto = Object.getPrototypeOf(obj);
    if(proto === obj) break;
    obj = proto;
    ++depth;
  } while(typeof obj == 'object' && obj != null);
  if(merge) {
    let i = 0;
    let result = {};
    for(let desc of a) for (let prop of GetKeys(desc)) if(!(prop in result)) result[prop] = desc[prop];
    return result;
  }
  return a;
}

export function define(obj, ...args) {
  let propdesc = {};
  for(let props of args) {
    let desc = Object.getOwnPropertyDescriptors(props);
    for(let prop of GetKeys(desc)) {
      propdesc[prop] = { ...desc[prop], enumerable: false, configurable: true };
      if('value' in propdesc[prop]) propdesc[prop].writable = true;
    }
  }
  Object.defineProperties(obj, propdesc);
  return obj;
}

export function weakDefine(obj, ...args) {
  let propdesc = {};
  for(let props of args) {
    let desc = Object.getOwnPropertyDescriptors(props);
    for(let prop of GetKeys(desc)) {
      if(prop in obj) continue;
      propdesc[prop] = { ...desc[prop], enumerable: false, configurable: true };
      if('value' in propdesc[prop]) propdesc[prop].writable = true;
    }
  }
  Object.defineProperties(obj, propdesc);
  return obj;
}

export function setHandlersFunction(os) {
  return function(fd, readable, writable) {
    //console.log('\x1b[38;5;82monFd\x1b[0m(', fd, ',', readable, ',', writable, ')');
    os.setReadHandler(fd, readable);
    os.setWriteHandler(fd, writable);
  };
}

/*export function setHandlers(os, handlers) {
  handlers.onFd = function(fd, readable, writable) {
    //console.log('\x1b[38;5;82monFd\x1b[0m(', fd, ',', readable, ',', writable, ')');
    os.setReadHandler(fd, readable);
    os.setWriteHandler(fd, writable);
  };
}*/

export function statusResponse(success, result_or_error, data) {
  let r = { success };
  if(result_or_error !== undefined) r[success ? 'result' : 'error'] = result_or_error;
  if(typeof data == 'object' && data != null && typeof data.seq == 'number') r.seq = data.seq;
  return r;
}

export function objectCommand(fn) {
  return function(data) {
    const respond = (success, result) => statusResponse(success, result, data);
    const { id, ...rest } = data;
    if(id in this.instances) {
      data.obj = this.instances[id];
      return fn.call(this, data, respond);
    }
    return respond(false, `No such object #${id}`);
  };
}

export function MakeListCommand(pred = v => typeof v != 'function', defaults = { maxDepth: Infinity }) {
  return objectCommand((data, respond) => {
    const { obj, enumerable = true, source = false, keyDescriptor = true, valueDescriptor = true } = data;
    defaults = { enumerable: true, writable: true, configurable: true, ...defaults };
    let propDesc = getPropertyDescriptors(obj, true, (proto, depth) => depth < (defaults.maxDepth ?? Infinity));
    let keys = GetKeys(propDesc);
    let map = keys.reduce((acc, key) => {
      const desc = propDesc[key];
      let value = desc?.value || obj[key];
      if(pred(value)) {
        if(valueDescriptor) {
          value = SerializeValue(value, source);
          for(let flag of ['enumerable', 'writable', 'configurable'])
            if(desc[flag] !== undefined) if (desc[flag] != defaults[flag]) value[flag] = desc[flag];
        } else if(typeof value == 'function') {
          value = value + '';
        }
        acc.push([keyDescriptor ? SerializeValue(key) : key, value]);
      }
      return acc;
    }, []);
    console.log('ListCommand', map);
    return respond(true, map);
  });
}

export function getPrototypeName(proto) {
  return proto.constructor?.name ?? proto[Symbol.toStringTag];
}

function DeserializeEntries(e) {
  if(Array.isArray(e)) return e.map(a => a.map(DeserializeValue));
  throw new Error(`DeserializeEntries e=${inspect(e)}`);
}

function DeserializeKeys(e) {
  if(Array.isArray(e)) return e.map(([k]) => DeserializeValue(k));
  throw new Error(`DeserializeKeys e=${inspect(e)}`);
}

function DeserializeMap(e) {
  return new Map(DeserializeEntries(e));
}

function DeserializeObject(e) {
  return Object.fromEntries(DeserializeEntries(e));
}

function ForwardMethods(e, ret = {}, thisObj) {
  let keys = DeserializeKeys(e);
  for(let key of keys) {
    ret[key] = MakeCommandFunction(key, o => o.connection, thisObj);
  }
  // console.log(`ForwardMethods`, { e, keys, ret });
  return ret;
}

function ForwardObject(e, thisObj) {
  let obj = ForwardMethods(e, {}, thisObj);
  console.log(`ForwardObject`, { e, obj, thisObj });
  return obj;
}

export function MakeCommandFunction(cmd, getConnection, thisObj, t) {
  const pfx = [`RESPONSE to`, typeof cmd == 'symbol' ? cmd : `"${cmd}"`];
  t ??= { methods: ForwardMethods, properties: DeserializeObject, symbols: DeserializeSymbols };
  if(typeof getConnection != 'function')
    getConnection = obj => (typeof obj == 'object' && obj != null && 'connection' in obj && obj.connection) || obj;

  // console.log('MakeCommandFunction', { cmd, getConnection, thisObj });

  return function(params = {}) {
    let client = getConnection(this);
    //console.log('MakeCommandFunction', { client });
    let r = client.sendCommand(cmd, params);
    console.log(`RESPONSE to '${cmd}'`, r);
    if(t[cmd]) r = t[cmd](r);
    return r;
  };
  return async function(params = {}) {
    let client = getConnection(this);
    await client.sendCommand(cmd, params);
    let r = await client.waitFor('response');
    if(t[cmd]) r = t[cmd](r);
    console.log(`RESPONSE to '${cmd}'`, r);
    return r;
  };
}

export function MakeSendFunction(sendFn, returnFn) {
  return returnFn ? msg => (sendFn(msg), returnFn()) : sendFn;
}

const TypedArrayPrototype = Object.getPrototypeOf(Uint32Array.prototype);
const TypedArrayConstructor = TypedArrayPrototype.constructor;

function isTypedArray(value) {
  try {
    return TypedArrayPrototype === Object.getPrototypeOf(Object.getPrototypeOf(value));
  } catch(e) {}
}

export function SerializeValue(value, source = false) {
  const type = typeof value;
  let desc = { type };

  if(type == 'object' && value != null) {
    desc['class'] = getPrototypeName(value) ?? getPrototypeName(Object.getPrototypeOf(value));
    //desc['chain'] = getPrototypeChain(value).map(getPrototypeName);
  } else if(type == 'symbol') {
    desc['description'] = value.description;
    desc['symbol'] = value.toString();
  } else if(type == 'function') {
    if(value.length !== undefined) desc['length'] = value.length;
  }
  if(type == 'object') {
    if(value instanceof ArrayBuffer) {
      let array = new Uint8Array(value);
      value = [...array];
      desc['class'] = 'ArrayBuffer';
      // delete desc['chain'];
    } else if(isTypedArray(value)) {
      value = [...value].map(n => (typeof n == 'number' ? n : n + ''));
    } else if(value instanceof Set) {
      value = [...value];
    }
  }

  if(typeof value == 'function') {
    if(source) desc.source = value + '';
  } else if(typeof value != 'symbol') {
    desc.value = value;
  }
  return desc;
}

export function DeserializeSymbols(names) {
  return names.map(n => n.replace(/Symbol\./, '')).map(n => Symbol[n]);
}

export function DeserializeValue(desc) {
  if(desc.type == 'symbol') return Symbol.for(desc.description);
  if(desc.type == 'object' && 'class' in desc) {
    let ctor = globalThis[desc.class];

    if(ctor && ctor !== Array) {
      desc.value = new ctor(desc.value);
    }
  }
  // if(desc.type=='string')
  return desc.value;
}

export const RPCConnect =
  (url, verbosity = 1) =>
  (new_ws = MakeWebSocket) =>
    new RPCSocket(url, RPCClient, verbosity).connect(new_ws);
export const RPCListen =
  (url, verbosity = 1) =>
  (new_ws = MakeWebSocket) =>
    new RPCSocket(url, RPCServer, verbosity).listen(new_ws);
