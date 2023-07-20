const IdSequencer = (start = 0, typedArr = Uint32Array, a) => ((a = new typedArr([start])), () => a[0]++);
const StringLength = str => str.replace(/\x1b\[[^m]*m/g, '').length;
const PadEnd = (str, n, ch = ' ') => str + ch.repeat(Math.max(0, n - StringLength(str)));
const Color = (str, ...args) => `\x1b[${args.join(';')}m${str}\x1b[0m`;
const Log = (...args) => console.log(console.config({ compact: 0, depth: 4, maxArrayLength: 2, maxStringLength: 64 }), ...args);
const LogMethod = (className, method, ...args) =>
  Log(PadEnd(className + Color('.', 1, 36) + Color(method, ...(method == 'constructor' ? [38, 5, 165] : method.startsWith('on') && method.length > 2 ? [1, 32] : [1, 33])), 25), ...args);

function isObject(value) {
  return typeof value == 'object' && value != null;
}

function isFunction(value) {
  return typeof value == 'function';
}

function isThenable(value) {
  return isObject(value) && isFunction(value.then);
}

const TypedArrayPrototype = Object.getPrototypeOf(Uint32Array.prototype);
const TypedArrayConstructor = TypedArrayPrototype.constructor;

function isTypedArray(value) {
  try {
    return TypedArrayPrototype === Object.getPrototypeOf(Object.getPrototypeOf(value));
  } catch(e) {}
}

function getPrototypeName(proto) {
  return proto[Symbol.toStringTag] ?? proto.constructor?.name;
}

/** @interface MessageReceiver */
export class MessageReceiver {
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
    return isFunction(sendMessage);
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

export const codecs = {
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
  },
  async bjson() {
    const { write, read } = await import('bjson');

    return { name: 'bjson', encode: v => write(v), decode: v => read(v) };
  },
  async js(verbose = false) {
    const { inspect } = await import('inspect');

    return {
      name: 'js',
      encode: v => inspect(v, { colors: false, compact: verbose ? false : -2 }),
      decode: v => eval(`(${v})`)
    };
  }
};

export function RPCApi(connection) {
  let api;
  api = define(new.target ? this : new RPCApi(c), { connection });

  return api;
}

for(let cmd of ['list', 'new', 'methods', 'properties', 'keys', 'names', 'symbols', 'call', 'set', 'get']) RPCApi.prototype[cmd] = MakeCommandFunction(cmd, o => o.connection);

export function RPCProxy(connection) {
  let obj = define(new.target ? this : new RPCProxy(c), { connection });

  return new Proxy(obj, {});
}

export function RPCObject(instance, connection) {
  let obj = define(new.target ? this : new RPCObject(instance), { connection, instance });

  return api.methods({ instance }).then(r => Object.assign(obj, r));
}

define(RPCObject.prototype, { [Symbol.toStringTag]: 'RPCObject' });

export function RPCFactory(api) {
  async function Factory(opts) {
    if(typeof opts == 'string') {
      const name = opts;
      opts = { class: name };
    }
    const instance = await api.new(opts);
    const { connection } = api;
    const obj = Object.setPrototypeOf(define({}, { instance, connection }), RPCObject.prototype);

    return define(obj, await api.methods(instance));
  }

  return Factory;
}

RPCFactory.prototype = function() {};

define(RPCFactory.prototype, { [Symbol.toStringTag]: 'RPCFactory' });

/**
 * @interface Connection
 */
export class Connection {
  static fromSocket = new WeakMap();

  makeId = IdSequencer(0);

  constructor(codec, verbose) {
    codec ??= 'json';

    define(this, {
      id: 0,
      exception: null,
      log: verbose ? (arg, ...args) => LogMethod(getPrototypeName(this), arg, ...args) : () => {}
    });

    define(this, typeof codec == 'string' && codecs[codec] ? { codecName: codec, codec: codecs[codec]() } : {});
    define(this, typeof codec == 'object' && codec.name ? { codecName: codec.name, codec } : {});

    (this.constructor ?? Connection).set.add(this);
  }

  error(message) {
    this.log(`ERROR: ${message}`);
    this.exception = new Error(message);
    this.log('error(', message, ')');
    this.close(1002, message.slice(0, 128));
    return this.exception;
  }

  close(status = 1000, reason = 'closed') {
    this.log('close(', status, reason, ')');
    this.socket.close(status, reason);
    delete this.fd;
    this.connected = false;
  }

  onmessage(msg) {
    const { codec } = this;

    if(!msg) return;
    if(typeof msg == 'string' && msg.trim() == '') return;

    //this.log('onmessage(0)', { msg, codec });

    let data;

    try {
      data = codec.decode((msg && msg.data) || msg);
    } catch(err) {
      /*throw this.error(`${codecName ?? codec?.name} parse error: ${(err && err.message) || msg}`);
      return this.exception;*/
      data = (msg && msg.data) || msg;
    }
    // this.log('onmessage(x)', { this: this, data });
    let response = this.processMessage(data);

    //this.log('onmessage(y)', { data, response });

    if(this.sendMessage) {
      if(isThenable(response)) response.then(r => this.sendMessage(r));
      else if(response !== undefined) this.sendMessage(response);
    }

    return response;
  }

  processMessage(data) {
    this.log('processMessage', { data });
    throw new Error('Virtual method');
  }

  onopen(data) {
    this.log('onopen:', data);
  }

  onconnect(data) {
    this.log('onconnect:', data);
  }

  onpong(data) {
    this.log('onpong:', data);
  }

  onerror(error) {
    this.log('onerror', error ? ` (${error})` : '');
    this.connected = false;
    this.cleanup();
  }

  onclose(code, why) {
    this.log('onclose', code, why ? ` (${why})` : '');
    this.connected = false;
    this.cleanup();
  }

  cleanup() {
    if(this.instances) for(let instance in this.instances) delete this.instances[instance];
  }

  sendMessage(obj) {
    this.log('sendMessage', obj);

    if(typeof obj.id == 'number') {
      if(this.messages && this.messages.requests) this.messages.requests[obj.id] = obj;
    } else {
      obj.id = this.makeId();
    }

    const msg = this.codec.encode(obj);

    if(this.send) return this.send(msg);
    if(this.socket) return this.socket.send(msg);
  }
}

define(Connection.prototype, { [Symbol.toStringTag]: 'RPCConnection' });
define(Connection, { codecs });

Connection.list = [];

export const RPC_PARSE_ERROR = -32700;
export const RPC_INVALID_REQUEST = -32600;
export const RPC_METHOD_NOT_FOUND = -32601;
export const RPC_INVALID_PARAMS = -32602;
export const RPC_INTERNAL_ERROR = -32603;
export const RPC_SERVER_ERROR_BASE = -32000; /* to -32099 */

export function RPCConstructorEndpoint(classes, instances) {
  const makeId = IdSequencer(0);

  const log = (...args) => LogMethod('RPCConstructorEndpoint', ...args);

  return {
    new(id, name, args = []) {
      let obj, ret, instance;

      try {
        obj = new classes[name](...args);
        instance = makeId();
        instances[instance] = obj;
      } catch({ message, stack }) {
        return statusResponse(id, false, { message, stack });
      }
      log('new', { name, args, obj, instance });

      return statusResponse(id, true, { instance });
    },
    list(id) {
      return statusResponse(id, true, Object.keys(classes));
    },
    delete(id, instance) {
      return statusResponse(id, delete instances[instance]);
    },
    invoke: instance2obj((id, obj, method, params = []) => {
      if(method in obj && isFunction(obj[method])) {
        const result = obj[method](...params);
        if(isThenable(result)) return result.then(result => statusResponse(id, true, result)).catch(error => statusResponse(id, false, error));
        return statusResponse(id, true, result);
      }
      return statusResponse(id, false, { message: `No such method on object #${instance}: ${method}` });
    }),
    keys: instance2obj((id, obj, enumerable = true) =>
      statusResponse(id, true, GetProperties(obj, enumerable ? obj => ('keys' in obj && isFunction(obj.keys) ? [...obj.keys()] : Object.keys(obj)) : obj => Object.getOwnPropertyNames(obj)))
    ),
    names: instance2obj((id, obj, enumerable = true) =>
      statusResponse(
        id,
        true,
        GetProperties(
          obj,
          Object.getOwnPropertyNames,
          (proto, depth) => proto != Object.prototype,
          (prop, obj, proto, depth) => isNaN(+prop)
        )
      )
    ),
    symbols: instance2obj((id, obj, enumerable = true) =>
      statusResponse(
        id,
        true,
        GetProperties(obj, Object.getOwnPropertySymbols).map(sym => sym.description)
      )
    ),
    properties: instanceMembers(v => !isFunction(v)),
    methods: instanceMembers(v => isFunction(v)),
    get: instance2obj((id, obj, property, instance) => {
      if(property in obj /* && !isFunction(obj[property])*/) {
        const result = obj[property];
        return statusResponse(id, true, SerializeValue(result));
      }

      return statusResponse(id, false, { message: `No such property on object #${instance}: ${property}` });
    }),
    set: instance2obj((id, obj, property, value) => statusResponse(id, true, (obj[property] = value)))
  };

  function instance2obj(fn) {
    return (id, instance, ...args) => {
      if(instance in instances) {
        const obj = instances[instance];
        return fn(id, obj, ...args);
      }

      return statusResponse(id, false, { message: `No such object #${instance}` });
    };
  }

  function instanceMembers(pred = v => !isFunction(v), defaults = { maxDepth: Infinity }) {
    return instance2obj((id, obj, enumerable = true, source = false, keyDescriptor = true, valueDescriptor = true) => {
      const propDesc = getPropertyDescriptors(obj, true, (proto, depth) => depth < (defaults.maxDepth ?? Infinity));

      return statusResponse(
        id,
        true,
        GetKeys(propDesc).reduce((acc, key) => {
          const desc = propDesc[key];
          let value = desc?.value || obj[key];

          if(pred(value)) {
            if(valueDescriptor) {
              value = SerializeValue(value, source);

              for(let flag of ['enumerable', 'writable', 'configurable']) if(!(flag in defaults) || desc[flag] == defaults[flag]) value[flag] = desc[flag];
            } else if(isFunction(value)) {
              value = value + '';
            }

            acc.push([keyDescriptor ? SerializeValue(key) : key, value]);
          }

          return acc;
        }, [])
      );
    });
  }
}

function statusResponse(id, success, resultOrError) {
  const response = { success };

  if(resultOrError !== undefined) response[success ? 'result' : 'error'] = resultOrError;

  if(typeof data == 'object' && data != null && 'id' in data) response.id = id;

  return response;
}

export class RPCServer extends Connection {
  #instances = {};

  constructor(classes, codec, verbose) {
    super(codec, verbose);
    console.log('this.log', this.log);

    this.log('constructor', { classes, codec });

    define(this, {
      classes,
      // messages: { requests: {}, responses: {} },
      handlers: {}
    });

    this.commands = RPCConstructorEndpoint(this.classes, this.#instances);

    RPCServer.set.add(this);
  }

  get instances() {
    return this.#instances;
  }

  processMessage(data) {
    this.log('processMessage(0)', data);

    if(!('command' in data)) return statusResponse(data.id, false, `No command specified`);

    const { command, id, params } = data;

    this.log('processMessage(1)', { command, id, params });

    const fn = this.commands[command];

    //if(typeof id == 'number') this.messages.requests[id] = data;

    this.log('processMessage(2)', { fn: (fn + '').replace(/\n.*/g, ''), id, command, params });
    let ret;
    try {
      /*if(isFunction(fn))*/ ret = fn(id, ...(params ?? []));
    } catch(error) {
      this.log('processMessage(ERROR)', error.message);
    }

    this.log('processMessage(3)', ret);
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
  constructor(classes, codec, verbose) {
    super(codec ?? codecs.json(false), verbose);

    this.log('constructor', { classes, codec });

    RPCClient.set.add(this);

    define(this, { classes, handlers: {} });

    /* let api;

    Object.defineProperties(this, {
      api: {
        get() {
          return api ?? (api = new RPCApi(this));
        }
      }
    });*/
  }

  processMessage(response) {
    const { success, error, result, id } = response;

    if('success' in response && success === false) {
      this.log('ERROR', error.message + '\n' + error.stack);
    } else {
      this.log('processMessage(1)', { success, error, result, id });
    }

    if(id !== undefined) if (id in this.handlers) this.handlers[id](result);
  }

  async sendCommand(command, ...params) {
    const message = { command, params, id: this.makeId() };

    //this.log('sendCommand', message);

    await this.sendMessage(message);

    return await new Promise((resolve, reject) => (this.handlers[message.id] = resolve));
  }
}

define(RPCClient.prototype, { [Symbol.toStringTag]: 'RPCClient' });

export class RPCSocket {
  fdlist = {};

  constructor(url, service = RPCServer, classes = {}, verbose) {
    define(this, { verbose, classes, service, url: typeof url != 'object' ? parseURL(url) : url, log: verbose ? (arg, ...args) => LogMethod(getPrototypeName(this), arg, ...args) : () => {} });

    RPCSocket.set.add(this);
  }

  get connections() {
    return Object.values(this.fdlist);
  }

  register(ctor) {
    const { classes } = this;

    if(typeof ctor == 'object' && ctor !== null) for(let name in ctor) classes[name] = ctor[name];
    else classes[ctor.name] = ctor;

    return this;
  }

  listen(createSocket = MakeWebSocket) {
    const { service, url } = this;
    this.log(`listen`, `${service.name} listening on ${url}`);
    this.listening = true;
    this.ws = createSocket(url, this.getCallbacks(), true);

    if(createSocket !== MakeWebSocket)
      if(this.ws.then) this.ws.then(() => (this.listening = false));
      else this.listening = false;

    return this;
  }

  connect(createSocket = MakeWebSocket) {
    const { service, url } = this;
    this.log(`connect`, `${service.name} connecting to ${url}`);
    this.ws = createSocket(url, this.getCallbacks(), false);
    return this;
  }

  getCallbacks(ctor = this.service) {
    let { fdlist, classes, verbose, log } = this;

    return {
      onConnect(sock) {
        log('onConnect', { sock, ctor: ctor.name, fdlist });

        try {
          fdlist[sock.fd] = new ctor(classes, 'json', verbose);
          fdlist[sock.fd].socket ??= sock;
          log('onConnect', { fdlist, sock });
          handle(sock, 'open');
        } catch(error) {
          log('onConnect', { error });
        }
      },
      onOpen(sock) {
        log('onOpen', { sock, ctor: ctor.name });
        fdlist[sock.fd] = new ctor(classes, 'json', verbose);
        fdlist[sock.fd].socket ??= sock;
        handle(sock, 'open');
      },
      onMessage(sock, msg) {
        log('onMessage', { sock, msg });
        handle(sock, 'message', msg);
      },
      onError(sock, error) {
        log('onError', { sock, error });
        callHandler(instance, 'error', error);
        handle(sock, 'error', error);
        remove(sock);
      },
      onClose(sock, code, why) {
        log('onClose', { sock, code, why });
        handle(sock, 'close', code, why);
        remove(sock);
      },
      onPong(sock, data) {
        log('onPong', { sock, data });
        handle(sock, 'pong', data);
      }
    };

    function handle(sock, event, ...args) {
      let conn;
      if((conn = fdlist[sock.fd])) callHandler(conn, event, ...args);
      else throw new Error(`No connection for fd #${sock.fd}!`);

      return { then: fn => (fn(sock.fd), obj) };
    }

    function remove(sock) {
      const { fd } = sock;
      delete fdlist[fd];
    }
  }
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
define(RPCSocket.prototype, { [Symbol.toStringTag]: 'RPCSocket' });

if(!isFunction(globalThis.WebSocket)) globalThis.WebSocket = function WebSocket() {};

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

  return ws;
}

function hasHandler(obj, eventName) {
  if(isObject(obj)) {
    const handler = obj['on' + eventName];

    if(isFunction(handler)) return handler;
  }
}

function callHandler(obj, eventName, ...args) {
  const fn = hasHandler(obj, eventName);
  if(fn) return fn.call(obj, ...args);
}

function parseURL(urlOrPort) {
  let protocol, host, port;

  if(!isNaN(+urlOrPort)) {
    [protocol, host, port] = ['ws', '0.0.0.0', urlOrPort];
  } else {
    [protocol = 'ws', host, port = 80] = [.../(.*:\/\/|)([^:/]*)(:[0-9]+|).*/.exec(urlOrPort)].slice(1);
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

function GetProperties(arg, method = Object.getOwnPropertyNames, pred = (proto, depth) => proto !== Object.prototype, propPred = (prop, obj, proto, depth) => true) {
  const set = new Set();
  let obj = arg,
    depth = 0;

  do {
    if(pred(obj, depth)) for(let prop of method(obj, depth)) if (propPred(prop, arg, obj, depth)) set.add(prop);

    const proto = Object.getPrototypeOf(obj);

    if(proto === obj) break;

    obj = proto;
    ++depth;
  } while(typeof obj == 'object' && obj != null);

  return [...set];
}

function GetKeys(obj, pred = (proto, depth) => proto !== Object.prototype, propPred = (prop, obj, proto, depth) => true) {
  let keys = new Set();

  for(let key of GetProperties(obj, Object.getOwnPropertyNames, pred, propPred)) keys.add(key);
  for(let key of GetProperties(obj, Object.getOwnPropertySymbols, pred, propPred)) keys.add(key);

  return [...keys];
}

function getPropertyDescriptors(obj, merge = true, pred = (proto, depth) => true) {
  let a = [],
    depth = 0,
    desc,
    ok;

  do {
    desc = Object.getOwnPropertyDescriptors(obj);

    try {
      ok = pred(obj, depth);
    } catch(e) {}

    if(ok) a.push(desc);

    const proto = Object.getPrototypeOf(obj);

    if(proto === obj) break;

    obj = proto;
    ++depth;
  } while(typeof obj == 'object' && obj != null);

  if(merge) {
    const result = {};

    for(let desc of a) for (let prop of GetKeys(desc)) if(!(prop in result)) result[prop] = desc[prop];

    return result;
  }

  return a;
}

function define(obj, ...args) {
  const propdesc = {};

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

/*export function weakDefine(obj, ...args) {
  const propdesc = {};

  for(let props of args) {
    const desc = Object.getOwnPropertyDescriptors(props);

    for(let prop of GetKeys(desc)) {
      if(prop in obj) continue;

      propdesc[prop] = { ...desc[prop], enumerable: false, configurable: true };

      if('value' in propdesc[prop]) propdesc[prop].writable = true;
    }
  }

  Object.defineProperties(obj, propdesc);
  return obj;
}*/

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
  const keys = DeserializeKeys(e);

  for(let key of keys) ret[key] = MakeCommandFunction(key, o => o.connection, thisObj);

  return ret;
}

/*function ForwardObject(e, thisObj) {
  const obj = ForwardMethods(e, {}, thisObj);

  Log(`ForwardObject`, { e, obj, thisObj });

  return obj;
}*/

function MakeCommandFunction(cmd, getConnection, thisObj, t) {
  const pfx = [`RESPONSE to`, typeof cmd == 'symbol' ? cmd : `"${cmd}"`];

  t ??= { methods: ForwardMethods, properties: DeserializeObject, symbols: DeserializeSymbols };

  if(!isFunction(getConnection)) getConnection = obj => (typeof obj == 'object' && obj != null && 'connection' in obj && obj.connection) || obj;

  // Log('MakeCommandFunction', { cmd, getConnection, thisObj });

  return function(params = {}) {
    const client = getConnection(this);
    //Log('MakeCommandFunction', { client });
    let r = client.sendCommand(cmd, params);
    this.log(`RESPONSE to '${cmd}'`, r);
    if(t[cmd]) r = t[cmd](r);
    return r;
  };

  return async function(params = {}) {
    const client = getConnection(this);
    await client.sendCommand(cmd, params);
    let r = await client.waitFor('response');
    if(t[cmd]) r = t[cmd](r);
    this.log(`RESPONSE to '${cmd}'`, r);
    return r;
  };
}

/*export function MakeSendFunction(sendFn, returnFn) {
  return returnFn ? msg => (sendFn(msg), returnFn()) : sendFn;
}*/

function SerializeValue(value, source = false) {
  const type = typeof value;
  let desc = { type };

  if(type == 'object' && value != null) {
    desc['class'] = getPrototypeName(value) ?? getPrototypeName(Object.getPrototypeOf(value));
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

  if(isFunction(value)) {
    if(source) desc.source = value + '';
  } else if(typeof value != 'symbol') {
    desc.value = value;
  }

  return desc;
}

function DeserializeSymbols(names) {
  return names.map(n => n.replace(/Symbol\./, '')).map(n => Symbol[n]);
}

function DeserializeValue(desc) {
  if(desc.type == 'symbol') return Symbol.for(desc.description);
  if(desc.type == 'object' && 'class' in desc) {
    let ctor = globalThis[desc.class];

    if(ctor && ctor !== Array) {
      desc.value = new ctor(desc.value);
    }
  }

  return desc.value;
}

export const RPCConnect =
  (url, verbosity = 1) =>
  (createSocket = MakeWebSocket) =>
    new RPCSocket(url, RPCClient, verbosity).connect(createSocket);

export const RPCListen =
  (url, verbosity = 1) =>
  (createSocket = MakeWebSocket) =>
    new RPCSocket(url, RPCServer, verbosity).listen(createSocket);
