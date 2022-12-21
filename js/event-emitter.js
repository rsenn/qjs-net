const indexOf = (haystack, needle) => Array.prototype.indexOf.call(haystack, needle);

export class EventEmitter {
  #events = {};

  constructor() {}

  on(event, listener) {
    if(!Array.isArray(this.#events[event])) this.#events[event] = [];
    this.#events[event].push(listener);
  }

  removeListener(event, listener) {
    const handlers = this.#events[event];
    if(Array.isArray(handlers)) {
      const idx = indexOf(handlers, listener);
      if(idx > -1) {
        handlers.splice(idx, 1);
        if(handlers.length == 0) delete this.#events[event];
      }
    }
  }

  removeAllListeners(event) {
    if(event) {
      if(Array.isArray(this.#events[event])) delete this.#events[event];
    } else {
      this.#events = {};
    }
  }

  rawListeners(event) {
    if(Array.isArray(this.#events[event])) return [...this.#events[event]];
  }

  emit(event, ...args) {
    const handlers = this.#events[event];
    if(Array.isArray(handlers)) for(let handler of handlers) handler.apply(this, args);
  }

  once(event, listener) {
    const callback = (...args) => {
      this.removeListener(event, callback);
      listener.apply(this, args);
    };
    callback.listener = listener;
    this.on(event, callback);
  }
}

EventEmitter.prototype[Symbol.toStringTag] = 'EventEmitter';
