import { close } from 'os';
import { exec } from 'os';
import { O_RDWR } from 'os';
import { open } from 'os';
import { readlink } from 'os';
import { stat } from 'os';
import { open as fopen } from 'std';

export function assert(actual, expected, message) {
  if(arguments.length == 1) expected = true;

  if(actual === expected) return;

  if(actual !== null && expected !== null && typeof actual == 'object' && typeof expected == 'object' && actual.toString() === expected.toString()) return;

  console.log('assert', { actual, expected, message });

  throw Error('assertion failed: got |' + actual + '|' + ', expected |' + expected + '|' + (message ? ' (' + message + ')' : ''));
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

export const randStr = (n, set = '_0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz', rng = Math.random) => {
  let o = '';
  while(--n >= 0) o += set[Math.round(rng() * (set.length - 1))];
  return o;
};

export const escape = s =>
  [
    [/\r/g, '\\r'],
    [/\n/g, '\\n'],
  ].reduce((a, [exp, rpl]) => a.replace(exp, rpl), s);

export const abbreviate = s => (s.length > 100 ? s.substring(0, 45) + ' ... ' + s.substring(-45) : s);

export async function save(generator, file) {
  let handle;

  for await(let chunk of await generator) {
    handle ??= fopen(file, 'w');

    if(chunk === undefined) break;

    console.log('Written:', handle.write(chunk, 0, chunk.byteLength));
  }

  handle.flush();
  handle.close();
}

export function MakeCert(sslCert, sslPrivateKey, hostname = 'localhost') {
  const stderr = open('/dev/null', O_RDWR);
  const ret = exec(['openssl', 'req', '-x509', '-out', sslCert, '-keyout', sslPrivateKey, '-newkey', 'rsa:2048', '-nodes', '-sha256', '-subj', '/CN=' + hostname], { stderr });
  close(stderr);
  return ret;
}

export default { assert, getpid, once, exists, randStr, escape, abbreviate, save, MakeCert, exists };
