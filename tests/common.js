import { open } from 'std';
import { readlink, stat } from 'os';

export function assert(actual, expected, message) {
  if(arguments.length == 1) expected = true;

  if(actual === expected) return;

  if(
    actual !== null &&
    expected !== null &&
    typeof actual == 'object' &&
    typeof expected == 'object' &&
    actual.toString() === expected.toString()
  )
    return;

  console.log('assert', { actual, expected, message });

  throw Error(
    'assertion failed: got |' + actual + '|' + ', expected |' + expected + '|' + (message ? ' (' + message + ')' : '')
  );
}

export const getpid = () => parseInt(readlink('/proc/self')[0]);

export const once = fn => {
  let ret,
    ran = false;
  return (...args) => (ran ? ret : ((ran = true), (ret = fn.apply(this, args))));
};

export const exists = path => {
  let [st, err] = stat(path);
  return !err;
};

export const randStr = (
  n,
  set = '_0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz',
  rng = Math.random
) => {
  let o = '';
  while(--n >= 0) o += set[Math.round(rng() * (set.length - 1))];
  return o;
};

export const escape = s =>
  [
    [/\r/g, '\\r'],
    [/\n/g, '\\n']
  ].reduce((a, [exp, rpl]) => a.replace(exp, rpl), s);

export const abbreviate = s => (s.length > 100 ? s.substring(0, 45) + ' ... ' + s.substring(-45) : s);

export async function save(generator, file) {
  let handle;
  console.log('generator:', generator);

  for await(let chunk of await generator) {
    handle ??= open(file, 'w');

    if(chunk === undefined) break;
    console.log('Writing:', chunk);

    handle.write(chunk, 0, chunk.byteLength);
  }

  handle.flush();
  handle.close();
}

export default { assert, getpid, once, exists, randStr, escape, abbreviate };
