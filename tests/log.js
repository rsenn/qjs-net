import net, { URL, LLL_ERR, LLL_WARN, LLL_NOTICE, LLL_INFO, LLL_DEBUG, LLL_PARSER, LLL_HEADER, LLL_EXT, LLL_CLIENT, LLL_LATENCY, LLL_USER, LLL_THREAD } from 'net';

export const Levels = Object.getOwnPropertyNames(net)
  .filter(n => /^LLL_/.test(n))
  .reduce((acc, n) => {
    let v = Math.log2(net[n]);
    if(Math.floor(v) === v) acc[net[n]] = n.substring(4);
    return acc;
  }, {});

export const DefaultLevels = LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO | LLL_CLIENT | LLL_LATENCY | LLL_USER | LLL_THREAD;

export const Init = name =>
  net.setLog(DefaultLevels, (level, msg) => {
    const l = Levels[level];
    const n = Math.log2(level);
    //return;
    if(level >= LLL_NOTICE && level <= LLL_EXT) return;

    if(l == 'USER') print(`${name.padEnd(8)} ${msg}`);
    else log(`${l.padEnd(10)} ${msg}`);
  });

export const Set = () =>
  net.setLog(net.LLL_USER | ((net.LLL_WARN << 1) - 1), (level, msg) => {
    const l = ['ERR', 'WARN', 'NOTICE', 'INFO', 'DEBUG', 'PARSER', 'HEADER', 'EXT', 'CLIENT', 'LATENCY', 'MINNET', 'THREAD'][level && Math.log2(level)] ?? level + '';
    if(l == 'NOTICE' || l == 'MINNET') console.log(('X', l).padEnd(8), msg.replace(/\r/g, '\\r').replace(/\n/g, '\\n'));
  });
