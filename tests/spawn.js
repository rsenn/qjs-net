import * as std from 'std';
import * as os from 'os';

export const getexe = () => os.readlink('/proc/self/exe')[0];
export const thisdir = () => {
  let [argv0] = scriptArgs;
  let re = /\/[^\/]*$/;
  if(re.test(argv0)) return argv0.replace(re, '');
  return '.';
};

export function spawn(script, ...args) {
  let argv = [getexe(), thisdir() + '/' + script].concat(args);
  let fd = os.open('child.log', os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644);
  let pid = os.exec(argv, { block: false, usePath: false, file: argv[0], stdin: fd, stdout: fd, stderr: fd });
  os.close(fd);
  return pid;
}

export function wait4(pid, status, options = 0) {
  let [ret, st] = os.waitpid(pid, options);

  ({ array: st => status.splice(0, status.length, st), object: st => (status.status = st), function: st => status(st) }[Array.isArray(status) ? 'array' : typeof status](st));
  return ret;
}
