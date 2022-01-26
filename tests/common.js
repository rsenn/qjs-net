import { readlink, stat } from 'os';

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

export default { getpid, once, exists };
