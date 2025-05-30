import { inspect } from 'inspect';

class JSValue {}
/* prettier-ignore */ class JSNumber extends JSValue { constructor(value, type) { super(); if(value !== undefined) this.value = value; if(type) this.type = type; } }
/* prettier-ignore */ class JSString extends JSValue { constructor(value) { super(); if(value !== undefined) this.value = value; } }
/* prettier-ignore */ class JSBoolean extends JSValue { constructor(value) { super(); if(value !== undefined) this.value = value; } }
/* prettier-ignore */ class JSSymbol extends JSValue { constructor(value, global) { super(); if(global) this.global=global;   if(value) this.value = value; } }
/* prettier-ignore */ class JSRegExp extends JSValue { constructor(source, flags) { super(); this.source = source; this.flags = flags; } }
/* prettier-ignore */ class JSObject extends JSValue { constructor(members, proto) { super(); if(members) this.members = members; if(proto) this.proto = proto; } }
/* prettier-ignore */ class JSArray extends JSObject { constructor(arr) { super(arr ? [...arr] : undefined); } }
/* prettier-ignore */ class JSFunction extends JSObject { constructor(code, members, proto) { super(members,proto); if(code) this.code = code; } }
/* prettier-ignore */ class JSProperty   { constructor(get, set) { if(get) this.get = get;  if(set) this.set = set; } }

define(JSValue.prototype, { [Symbol.toStringTag]: 'JSValue' });
define(JSNumber.prototype, { [Symbol.toStringTag]: 'JSNumber', type: 'number' });
define(JSString.prototype, { [Symbol.toStringTag]: 'JSString', type: 'string' });
define(JSBoolean.prototype, { [Symbol.toStringTag]: 'JSBoolean', type: 'boolean' });
define(JSSymbol.prototype, { [Symbol.toStringTag]: 'JSSymbol', type: 'symbol' });
define(JSRegExp.prototype, { [Symbol.toStringTag]: 'JSRegExp', type: 'regexp' });
define(JSObject.prototype, { [Symbol.toStringTag]: 'JSObject', type: 'object' });
define(JSArray.prototype, { [Symbol.toStringTag]: 'JSArray', type: 'array' });
define(JSFunction.prototype, { [Symbol.toStringTag]: 'JSFunction', type: 'function' });
define(JSProperty.prototype, { [Symbol.toStringTag]: 'JSProperty', type: 'property' });

Object.assign(globalThis, { JSValue, JSNumber, JSString, JSBoolean, JSSymbol, JSRegExp, JSObject, JSArray });

const TypedArrayPrototype = Object.getPrototypeOf(Uint32Array.prototype);

export function EncodeJS(val) {
  const type = typeof val;
  let info;

  switch (type) {
    case 'function':
    case 'object':
      if(type == 'object' && val === null) {
        info = new JSValue();
        info.type = 'null';
        break;
      }
      const isFunction = type == 'function';
      const isArray = type == 'object' && Array.isArray(val) && val !== Array.prototype;
      const isTypedArray = (Object.getPrototypeOf(val) == TypedArrayPrototype || val instanceof TypedArrayPrototype.constructor) && val !== TypedArrayPrototype;
      const isArrayBuffer = (Object.getPrototypeOf(val) == ArrayBuffer.prototype || val instanceof ArrayBuffer) && val !== ArrayBuffer.prototype;
      if(isFunction) info = new JSFunction(/=>/.test(val + '') ? (val + '').replace(/^\((.*)\)\s*=>\s/, '($1) => ') : (val + '').replace(/^(function\s*|)(.*)/, '(function $2)'));
      else if(val instanceof RegExp) info = new JSRegExp(val.source, val.flags);
      else if(isArray || isTypedArray) info = new JSArray(isTypedArray ? null : [...val].map(v => EncodeJS(v)));
      else info = new JSObject();
      const proto = Object.getPrototypeOf(val);
      const tag = (proto ? proto[Symbol.toStringTag] : val[Symbol.toStringTag]) ?? (proto != Object.prototype && val?.constructor?.name);
      if(tag) info.class = tag;
      if(isArrayBuffer) info.data = [...new Uint8Array(val)].reduce((a, n) => (a ? a + ',' : '') + n.toString(16).padStart(2, '0'), '');
      if(!isFunction) {
        const members = isArray || isTypedArray ? [...val].map(i => EncodeJS(i)) : EncodeObj(val);
        if(isTypedArray) (info.members ??= {}).buffer = EncodeJS(val.buffer);
        else if(Object.keys(members).length > 0) info.members = members;
        if(!(isArray || isTypedArray)) if (proto) define(info, { proto: EncodeJS(proto) });
      }
      break;
    case 'number':
    case 'bigfloat':
    case 'bigint':
    case 'bigdecimal':
      info = new JSNumber(undefined, type == 'number' ? undefined : type);
      info.value = val + '';
      break;
    case 'string':
      info = new JSString(val);
      break;
    case 'boolean':
      info = new JSBoolean(val + '');
      break;
    case 'undefined':
      info = new JSValue();
      info.type = type;
      break;
    case 'symbol':
      const str = val.toString();
      if(/Symbol\(Symbol\.([^)]*)\)/.test(str)) info = new JSSymbol(undefined, str.replace(/Symbol\(Symbol\.([^)]*)\)/g, '$1'));
      else info = new JSSymbol(str.replace(/Symbol\(([^)]*)\)/g, '$1'));
      break;
    default:
      info = new JSValue();
      info.type = type;
      info.value = val + '';
      break;
  }

  return info;
}

export function EncodeObj(obj) {
  const members = Object.setPrototypeOf({}, null);
  const props = Object.getOwnPropertyDescriptors(obj);
  for(let k in props) {
    const a = props[k];
    let r;
    if(a.get || a.set) r = new JSProperty(a.get && (a.get + '').replace(/^[^{]*{/, '() => {'), a.set && (a.set + '').replace(/^[^{]*{/, '() => {'));
    else r = EncodeJS(obj[k]);
    if(a.enumerable === false) define(r, { enumerable: a.enumerable });
    if(a.configurable === false) define(r, { configurable: a.configurable });
    if(!('value' in a) && 'writable' in a) define(r, { writable: a.writable });
    members[k] = r;
  }
  return members;
}

export function DecodeJS(info) {
  let val;

  switch (info.type) {
    case 'array':
    case 'function':
    case 'object':
      if(info.type == 'function')
        try {
          val = eval(info.code);
        } catch(e) {
          console.log('eval', info.code);
        }
      else if(info.type == 'array') {
        if(info.class && info.class != 'Array' && info.members.buffer) {
          const { buffer, ...members } = info.members;
          info.members = members;
          val = new globalThis[info.class](DecodeJS(buffer));
        } else if(Array.isArray(info.members)) val = [...info.members].map(i => DecodeJS(i));
      } else if(info.class == 'ArrayBuffer') val = new Uint8Array(info.data.split(',').map(s => parseInt(s, 16))).buffer;
      else val = {};
      if(info.members) {
        const props = {};
        for(const k in info.members) {
          const v = info.members[k];
          if(v.get || v.set) {
            const prop = { enumerable: true, configurable: true };
            if(v.get) prop.get = eval(v.get);
            if(v.set) prop.set = eval(v.set);
            if(prop.flags) prop = { ...prop, ...prop.flags };
            props[k] = prop;
          } else {
            props[k] = { enumerable: true, writable: true, configurable: true, value: DecodeJS(v) };
          }
        }
        Object.defineProperties(val, props);
      }
      break;
    case 'boolean':
      val = info.value == 'true' ? true : false;
      break;
    case 'bigint':
      val = BigInt(info.value);
      break;
    case 'bigfloat':
      val = BigFloat(info.value);
      break;
    case 'bigdecimal':
      val = BigDecimal(info.value);
      break;
    case 'number':
      val = Number(info.value);
      break;
    case 'string':
      val = info.value + '';
      break;
    case 'regexp':
      val = new RegExp(info.source, info.flags);
      break;
    case 'symbol':
      val = info.global ? Symbol[info.global] : Symbol.for(info.value);
      break;
    case 'undefined':
      val = undefined;
      break;
    case 'null':
      val = null;
      break;
    default:
      val = info.value;
      break;
  }

  return val;
}

function define(obj, ...args) {
  for(let other of args) {
    const props = {},
      desc = Object.getOwnPropertyDescriptors(other),
      keys = [...Object.getOwnPropertyNames(other), ...Object.getOwnPropertySymbols(other)];
    for(let k of keys) props[k] = { enumerable: false, configurable: true, writable: true, value: desc[k].value };
    Object.defineProperties(obj, props);
  }
  return obj;
}
