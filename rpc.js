let sockId;

function isThenable(value) {
  return typeof value == 'object' && value != null && typeof value.then == 'function';
}

function hasHandler(obj, eventName) {
  if(typeof obj == 'object' && obj != null) {
    const handler = obj['on' + eventName];
    if(typeof handler == 'function') return handler;
  }
}

function callHandler(obj, eventName, ...args) {
  let ret, fn;
  if((fn = hasHandler(obj, eventName))) ret = fn(...args);
  return ret;
}

function parseURL(url_or_port) {
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
  }

  return {
    protocol,
    host,
    port,
    toString() {
      const { protocol, host, port } = this;
      return `${protocol || 'ws'}://${host}:${port}`;
    }
  };
}

function define(obj, props) {
  let propdesc = {};
  for(let prop in props)
    propdesc[prop] = { value: props[prop], enumerable: false, configurable: true, writable: true };
  Object.defineProperties(obj, propdesc);
}

function setHandlers(os, handlers) {
  handlers.onFd = function(fd, readable, writable) {
    os.setReadHandler(fd, readable);
    os.setWriteHandler(fd, writable);
  };
}

/** @interface MessageReceiver */
export class MessageReceiver {
  static [Symbol.hasInstance](instance) {
    return 'onmessage' in instance;
  }
  onmessage = null;
}

/** @interface MessageTransmitter */
export class MessageTransmitter {
  static [Symbol.hasInstance](instance) {
    return typeof sendMessage == 'function';
  }
  /** @abstract */
  sendMessage() {}
}

/** @interface MessageTransceiver */
export function MessageTransceiver() {}

Object.assign(MessageTransceiver.prototype, MessageReceiver.prototype, MessageTransmitter.prototype);

Object.defineProperty(MessageTransceiver, Symbol.hasInstance, {
  value: instance => [MessageReceiver, MessageTransmitter].every(ctor => ctor[Symbol.hasInstance](instance))
});

const codecs = {
  none: {
    encode: v => v,
    decode: v => v
  },
  json: {
    encode: v => JSON.stringify(v),
    decode: v => JSON.parse(v)
  }
};

/**
 * @interface Connection
 */
export class Connection {
  constructor(socket, instance, log) {
    this.socket = socket;
    this.log = (...args) => {
      log(`${this.constructor.name} (fd ${this.socket.fd})`, ...args);
    };
    this.log('connected');
  }

  onmessage(msg) {
    let data;
    try {
      data = JSON.parse(msg);
    } catch(err) {
      const e = `JSON parse error: '${msg ?? err.message}'`;
      this.log(e);
      throw new Error(`RPCServerConnection (#${this.socket}) ${e}`);
    }
    this.log('onmessage', data);
    let response = this.processMessage(data);
    if(isThenable(response)) response.then(r => this.sendMessage(r));
    else if(response !== undefined) this.sendMessage(response);
  }

  processMessage(data) {
    this.log('message:', data);
  }

  onpong(data) {
    this.log('pong:', data);
  }
  onerror(error) {
    this.log('error' + (error ? ` (${error})` : ''));
  }
  onclose(why) {
    this.log('closed' + (reason ? ` (${reason})` : ''));
  }

  sendMessage(obj) {
    let msg = typeof obj != 'string' ? JSON.stringify(obj) : obj;
    this.log('sending', msg);
    this.socket.send(msg);
  }

  static getCallbacks(fdlist, classes, instance, log, verbosity = 0) {
    const ctor = this;
    let handlers;

    const handle = (sock, event, ...args) => {
      let conn, obj;
      if((conn = handlers.fdlist[sock.fd])) callHandler(conn, event, ...args);
      obj = { then: fn => (fn(sock.fd), obj) };
      return obj;
    };

    const remove = sock => {
      handlers.fdlist[sock.fd] = null;
      delete handlers.fdlist[sock.fd];
    };

    /*log = () => {};*/
    const verbose = verbosity ? (...args) => log('VERBOSE', ...args) : () => {};

    handlers = {
      fdlist,
      onConnect(sock) {
        verbose(`Connected`, { fd: sock.fd }, ctor.name);
        fdlist[sock.fd] = new ctor(sock, classes, instance, log);
        handle(sock, 'open');
      },
      onMessage(sock, msg) {
        verbose(`Message`, { fd: sock.fd }, msg);
        handle(sock, 'message', msg);
      },
      onError(sock, error) {
        verbose(`Error`, { fd: sock.fd }, error);
        callHandler(instance, 'error', error);
        handle(sock, 'error', error) /*.then(fd => delete fdlist[fd])*/;
        remove(sock);
      },
      onClose(sock, why) {
        verbose(`Closed`, { fd: sock.fd }, why);
        handle(sock, 'close', why);
        remove(sock);
      },
      onPong(sock, data) {
        verbose(`Pong`, { fd: sock.fd }, data);
        handle(sock, 'pong', data);
      }
    };
    return handlers;
  }
}
Object.defineProperty(Connection.prototype, Symbol.toStringTag, { value: 'Connection' });

export class RPCServerConnection extends Connection {
  constructor(socket, classes, instance, log) {
    super(socket, instance, log);

    this.classes = classes;
    this.instances = {};
    this.lastId = 0;
    this.connected = true;
  }

  makeId() {
    return ++this.lastId;
  }

  onclose(reason) {
    this.connected = false;
    for(let id in this.instances) delete this.instances[id];

    super.onclose(reason);
  }

  static commands = {
    new: function({ name, args = [] }) {
      let obj, ret;
      try {
        obj = new this.classes[name](...args);
        const id = this.makeId();
        this.instances[id] = obj;
        ret = { success: true, id, name };
      } catch(e) {
        ret = status(false, e.message);
      }
      return ret;
    }
  };

  processMessage(data) {
    let ret = null;
    const { command, seq } = data;
    const { commands } = this.constructor;

    this.log('message:', data);
    if(commands[command]) return commands[command](data);

    switch (command) {
      case 'new': {
        const { name, args = [] } = data;

        break;
      }
      case 'delete': {
        const { id } = data;
        if(id in this.instances) {
          delete this.instances[id];
          ret = status(true);
        } else {
          ret = status(false, `No such object #${id}`);
        }
        break;
      }
      case 'call': {
        const { id, method, args = [] } = data;
        if(id in this.instances) {
          const obj = this.instances[id];

          if(method in obj && typeof obj[method] == 'function') {
            const result = obj[method](...args);

            if(isThenable(result))
              ret = result.then(result => status(true, result)).catch(error => status(false, error));
            else ret = status(true, result);
          } else {
            ret = status(false, `No such method on object #${id}: ${method}`);
          }
        } else {
          ret = status(false, `No such object #${id}`);
        }
        break;
      }
    }

    return ret;
  }
  static get name() {
    return 'RPC server';
  }
}

Object.defineProperty(RPCServerConnection.prototype, Symbol.toStringTag, {
  value: 'RPCServerConnection'
});

function status(success, result_or_error) {
  let r = { success };
  if(result_or_error !== undefined) r[success ? 'result' : 'error'] = result_or_error;
  if(typeof seq == 'number') r.seq = seq;
  return r;
}
/**
 * This class describes a client connection.
 *
 * @class      RPCClientConnection
 * @param      {Object} socket
 * @param      {Object} classes
 *
 */
export class RPCClientConnection extends Connection {
  constructor(socket, classes, instance, log) {
    super(socket, instance, log);

    this.instances = {};
    this.connected = true;
  }

  onclose(reason) {
    this.connected = false;

    for(let id in this.instances) delete this.instances[id];

    super.onclose(reason);
  }

  processMessage(response) {
    const { success, error, result } = response;
    this.log('message:', response);
  }

  static get name() {
    return 'RPC client';
  }

  [Symbol.toStringTag] = 'RPCClientConnection';
}
Object.defineProperty(RPCClientConnection.prototype, Symbol.toStringTag, {
  value: 'RPCClientConnection'
});

/**
 * Creates new RPC socket
 *
 * @class      RPCSocket (name)
 * @param      {string}     [url=window.location.href]     URL (ws://127.0.0.1) or Port
 * @param      {function}   [service=RPCServerConnection]  The service constructor
 * @return     {RPCSocket}  The RPC socket.
 */
export function RPCSocket(url, service = RPCServerConnection, verbosity = 1) {
  const instance = new.target ? this : new RPCSocket(url, service);
  const fdlist = {};
  const classes = {};

  const log = (msg, ...args) => {
    const { console } = globalThis;
    (instance.log ?? console.log)(
      msg,
      console.config({
        multiline: false,
        compact: false,
        maxStringLength: 100,
        stringBreakNewline: false
      }),
      ...args
    );
  };

  const callbacks = service.getCallbacks(fdlist, classes, instance, log, verbosity);

  if(!url) url = globalThis.location?.href;
  if(typeof url != 'object') url = parseURL(url);

  define(instance, {
    fdlist,
    classes,
    callbacks,
    url,
    register(ctor) {
      if(typeof ctor == 'object' && ctor !== null) {
        for(let name in ctor) classes[name] = ctor[name];
      } else {
        classes[ctor.name] = ctor;
      }
      return this;
    },
    listen(new_ws, os = globalThis.os) {
      if(!new_ws) new_ws = MakeWebSocket;
      log(`${service.name} listening on ${this.url}`);
      if(os) setHandlers(os, callbacks);
      this.listening = true;
      this.ws = new_ws(this.url, callbacks, true);
      if(new_ws !== MakeWebSocket)
        if(this.ws.then) this.ws.then(() => (this.listening = false));
        else this.listening = false;
      return this;
    },
    connect(new_ws, os = globalThis.os) {
      if(!new_ws) new_ws = MakeWebSocket;
      log(`${service.name} connecting to ${this.url}`);
      if(os) setHandlers(os, callbacks);
      this.ws = new_ws(this.url, callbacks, false);
      return this;
    },
    /* prettier-ignore */ get connected() {
      const {ws}= this;
      return ws && typeof ws.readyState == 'number' ? ws.readyState <= ws.OPEN : false;
    }
  });

  return instance;
}

Object.defineProperty(RPCSocket.prototype, Symbol.toStringTag, { value: 'RPCSocket' });

if(globalThis.WebSocket) {
  function MakeWebSocket(url, callbacks) {
    let ws;
    try {
      ws = new WebSocket(url + '');
    } catch(error) {
      callbacks.onError(ws, error);
      return null;
    }
    ws.onopen = () => callbacks.onConnect(ws);
    ws.onerror = error => callbacks.onError(ws, error);
    ws.onmessage = msg => callbacks.onMessage(ws, msg);
    ws.onpong = pong => callbacks.onPong(ws, pong);
    ws.onclose = reason => callbacks.onClose(ws, reason);
    ws.fd = sockId = (sockId | 0) + 1;

    return ws;
  }
}

/*if(globalThis.scriptArgs && scriptArgs[0].endsWith('rpc.js')) {
  class TestClass {
    method(...args) {
      console.log('TestClass.method args =', args);
      return [1, 2, 3, 4];
    }

    async asyncMethod(...args) {
      console.log('TestClass.asyncMethod args =', args);
      return [1, 2, 3, 4];
    }
  }
  const SERVER = 0,
    CLIENT = 1;
  const modes = { socket: +scriptArgs[1]?.startsWith('c') };
  if(scriptArgs[2]) modes.protocol = +scriptArgs[2]?.startsWith('c');
  else modes.protocol = SERVER;

  if(modes.socket) {
    try {
      const rpc = new RPCSocket(9200, modes.protocol ? RPCClientConnection : RPCServerConnection);
      rpc.register({ TestClass });
      import('os').then(os =>
        import('net').then(({ client }) =>
          rpc.connect((url, callbacks) => client({ ...url, ...callbacks }), os)
        )
      );
    } catch(e) {
      console.log('ERROR: ' + e.message);
    }
  } else {
    try {
      const rpc = new RPCSocket(9200, modes.protocol ? RPCClientConnection : RPCServerConnection);
      rpc.register({ TestClass });
      import('os').then(os =>
        import('net').then(({ server }) =>
          rpc.listen((url, callbacks) => server({ ...url, ...callbacks }), os)
        )
      );
    } catch(e) {
      console.log('ERROR: ' + e.message);
    }
  }
}*/

export default {
  ServerConnection: RPCServerConnection,
  ClientConnection: RPCClientConnection,
  Socket: RPCSocket,
  MessageReceiver,
  MessageTransmitter,
  MessageTransceiver
};
