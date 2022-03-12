import { readlink, open, O_WRONLY, O_CREAT, O_TRUNC, exec, close, waitpid } from 'os';

export const getexe = () => readlink('/proc/self/exe')[0];
export const thisdir = () => {
  let [argv0] = scriptArgs;
  let re = /\/[^\/]*$/;
  if(re.test(argv0)) return argv0.replace(re, '');
  return '.';
};

export function spawn(script, ...args) {
  let argv = [getexe(), thisdir() + '/' + script].concat(args);
  let fd = open('child.log', O_WRONLY | O_CREAT | O_TRUNC, 0o644);
  let pid = exec(argv, { block: false, usePath: false, file: argv[0], stdin: fd, stdout: fd, stderr: fd });
  close(fd);
  return pid;
}

export function wait4(pid, status, options = 0) {
  let [ret, st] = waitpid(pid, options);

  ({ array: st => status.splice(0, status.length, st), object: st => (status.status = st), function: st => status(st) }[
    Array.isArray(status) ? 'array' : typeof status
  ](st));
  return ret;
}
