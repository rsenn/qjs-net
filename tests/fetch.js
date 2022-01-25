import * as std from 'std';
import * as os from 'os';

const fetchArgs = {
  curl: (url, file) => ['-s', '-k', '-L', url].concat(file ? ['-o', file] : []),
  fetch: (url, file) => ['--quiet', '--no-verify-peer', '-o', file ?? '/dev/stdout', url],
  wget: (url, file) => ['--quiet', '--no-check-certificate', '--content-disposition', '-O', file ?? '-', url]
};

const programs = Object.keys(fetchArgs);

let fetchProgram;

export function Fetch(url, file) {
  let ret,
    options = {},
    pipe;

  if(!(file ?? false)) {
    [pipe, options.stdout] = os.pipe();
    //    options.block=false;
  }

  std.err.puts(`Fetching ${url} ...\n`);

  for(let p of fetchProgram ? [fetchProgram] : programs) {
    let args = [p].concat(fetchArgs[p](url, file));
    if((ret = os.exec(args, options)) == 0) {
      fetchProgram = p;
      break;
    }
  }

  if(pipe) {
    os.close(options.stdout);
    let f = std.fdopen(pipe, 'r');
    let str = f.readAsString();

    f.close();
    os.close(pipe);

    os.waitpid(ret, os.WNOHANG);
    ret = str;
  }

  return ret;
}
export const TestFetch = (host, port) => (location, file) => Fetch(`http://${host}:${port}/${location}`, file);

export default Fetch;
