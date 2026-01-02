import { close } from 'os';
import { exec } from 'os';
import { O_CREAT } from 'os';
import { O_TRUNC } from 'os';
import { O_WRONLY } from 'os';
import { open } from 'os';
import { readlink } from 'os';
import { waitpid } from 'os';

export { WNOHANG } from 'os';

export const getexe = () => readlink('/proc/self/exe')[0];
export const thisdir = () => {
  let [argv0] = scriptArgs;
  let re = /\/[^\/]*$/;
  if(re.test(argv0)) return argv0.replace(re, '');
  return '.';
};

export function spawn(script, args = [], log) {
  let argv = [getexe(), thisdir() + '/' + script].concat(args);
  let fd;
  let pid = exec(argv, {
    block: false,
    usePath: false,
    file: argv[0],
    ...(() => {
      let o = {};
      if(log) {
        fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0o644);
        console.log('opened', log, fd);
        o.stdin = o.stdout = o.stderr = fd;
      }
      return o;
    })(),
  });
  if(fd) close(fd);
  return pid;
}

export function wait4(pid, status, options = 0) {
  let [ret, st] = waitpid(pid, options);

  ({
    array: st => status.splice(0, status.length, st),
    object: st => (status.status = st),
    function: st => status(st),
  })[Array.isArray(status) ? 'array' : typeof status](st);
  return ret;
}