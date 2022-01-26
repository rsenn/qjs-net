import net, { URL, LLL_ERR, LLL_WARN, LLL_NOTICE, LLL_INFO, LLL_DEBUG, LLL_PARSER, LLL_HEADER, LLL_EXT, LLL_CLIENT, LLL_LATENCY, LLL_USER, LLL_THREAD } from 'net';

export const Levels = Object.keys({ LLL_ERR, LLL_WARN, LLL_NOTICE, LLL_INFO, LLL_DEBUG, LLL_PARSER, LLL_HEADER, LLL_EXT, LLL_CLIENT, LLL_LATENCY, LLL_USER, LLL_THREAD })
  .reduce((acc, n) => {
    let v = Math.log2(net[n]);
    if(Math.floor(v) === v) acc[net[n]] = n.substring(4);
    return acc;
  }, {});

export const DefaultLevels = LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO | LLL_CLIENT | LLL_LATENCY | LLL_USER | LLL_THREAD;

export const Init = name =>
  net.setLog(DefaultLevels, (level, msg) => {
    let l = Levels[level];
    //return;
    if(level >= LLL_NOTICE && level <= LLL_EXT) return;

    if(l == 'USER') l = name ?? l;
    console.log(`${l.padEnd(10)} ${msg}`);
  });

export const SetLog = name =>
  net.setLog(net.LLL_USER | ((net.LLL_WARN << 1) - 1), (level, msg) => {
    let l = Levels[level] ?? 'UNKNOWN';
    if(l == 'USER') l = name ?? l;
    console.log(('X', l).padEnd(8), msg.replace(/\r/g, '\\r').replace(/\n/g, '\\n'));
  });
