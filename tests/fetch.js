import { close } from 'os';
import { exec } from 'os';
import { pipe } from 'os';
import { waitpid } from 'os';
import { WNOHANG } from 'os';
import { err } from 'std';
import { fdopen } from 'std';

const fetchArgs = {
  curl: (url, file) => ['-s', '-k', '-L', url].concat(file ? ['-o', file] : []),
  fetch: (url, file) => ['--quiet', '--no-verify-peer', '-o', file ?? '/dev/stdout', url],
  wget: (url, file) => ['--quiet', '--no-check-certificate', '--content-disposition', '-O', file ?? '-', url],
};

const programs = Object.keys(fetchArgs);

let fetchProgram;

export function Fetch(url, file) {
  let ret,
    options = {},
    pipe;

  if(!(file ?? false)) {
    [pipe, options.stdout] = pipe();
    //    options.block=false;
  }

  err.puts(`Fetching ${url} ...\n`);

  for(let p of fetchProgram ? [fetchProgram] : programs) {
    let args = [p].concat(fetchArgs[p](url, file));
    if((ret = exec(args, options)) == 0) {
      fetchProgram = p;
      break;
    }
  }

  if(pipe) {
    close(options.stdout);
    let f = fdopen(pipe, 'r');
    let str = f.readAsString();

    f.close();
    close(pipe);

    waitpid(ret, WNOHANG);
    ret = str;
  }

  return ret;
}

export const TestFetch = (host, port) => (location, file) => Fetch(`http://${host}:${port}/${location}`, file);

export default Fetch;
