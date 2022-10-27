import { setLog, URL, LLL_ERR, LLL_WARN, LLL_NOTICE, LLL_INFO, LLL_DEBUG, LLL_PARSER, LLL_HEADER, LLL_EXT, LLL_CLIENT, LLL_LATENCY, LLL_USER, LLL_THREAD } from 'net';
import { err } from 'std';

export const Levels = (() => {
  const llObj = {
    LLL_ERR,
    LLL_WARN,
    LLL_NOTICE,
    LLL_INFO,
    LLL_DEBUG,
    LLL_PARSER,
    LLL_HEADER,
    LLL_EXT,
    LLL_CLIENT,
    LLL_LATENCY,
    LLL_USER,
    LLL_THREAD
  };
  return Object.keys(llObj).reduce((acc, n) => {
    let v = Math.log2(llObj[n]);
    if(Math.floor(v) === v) acc[llObj[n]] = n.substring(4);
    return acc;
  }, {});
})();

export const isDebug = () => scriptArgs.some(a => /^-[dx]/.test(a));
export const DebugCallback = fn => (isDebug() ? fn : () => {});

export const DefaultLevels =
  LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO | LLL_CLIENT | LLL_LATENCY | LLL_USER | LLL_THREAD;

export const Init = (name, mask = LLL_USER | ((LLL_CLIENT << 1) - 1)) =>
  setLog(
    mask,
    DebugCallback((level, msg) => {
      let l = Levels[level];
      if(!(level & mask)) return;
      if(level >= LLL_NOTICE && level <= LLL_EXT) return;
      if(l == 'USER') l = name ?? l;
      err.puts(`${l.padEnd(10)} ${msg}\n`);
    })
  );

export const SetLog = (name, maxLevel = LLL_CLIENT) =>
  setLog(LLL_USER | ((maxLevel << 1) - 1), (level, msg) => {
    let l = Levels[level] ?? 'UNKNOWN';
    if(l == 'USER') l = name ?? l;
    err.puts(('X', l).padEnd(9) + msg.replace(/\r/g, '\\r').replace(/\n/g, '\\n'));
  });

import('console').then(({ Console }) => { globalThis.console = new Console(err, { inspectOptions: { compact: 0, customInspect: true, maxStringLength: 100 } });
});

export const log = (() => {
  const name = scriptArgs[0].replace(/.*\//g, '');
  return (...args) => console.log(name + ':', ...args);
})();
