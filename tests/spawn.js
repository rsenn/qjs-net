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
  let file = thisdir() + '/' + script;
  let exe = getexe();
  let fd = os.open('child.log', os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644);
  let argv = [exe, file].concat(args);
  console.log('spawn', { fd, argv, file });
  let pid = os.exec(argv, { block: false, usePath: false, file: exe, stdout: fd, stderr: fd });
  os.close(fd);
  console.log('spawned', argv.join(' '));
  return pid;
}
