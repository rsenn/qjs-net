import * as std from 'std';
import * as os from 'os';
import { Console } from 'console';
import * as deep from 'deep';
import { decorate, define, memoize, histogram, lazyProperties } from 'util';

let src2obj = {};
let files = {},
  all = new Set();
let definitions = {},
  lookup = {},
  used = new Set(),
  external = new Set();

function main() {
  globalThis.console = new Console({
    inspectOptions: { compact: 2, maxArrayLength: Infinity, maxStringLength: 200 }
  });

  //epl.inspectOptions.maxStringLength=20;

  let commands = ReadJSON('build/x86_64-linux-debug/compile_commands.json');

  Object.assign(globalThis, {
    GetFunctionFromIndex,
    GetSymbol,
    MatchSymbols,
    GetObj,
    PipeStream,
    SplitByPred,
    SymbolsToDefinedUndefined,
    ReadJSON,
    YieldAll,
    Within,
    ArrayArgs,
    Negate,
    decorate,
    Chain,
    Transform
  });
  Object.assign(
    globalThis,
    decorate(YieldAll, {
      GetComments,
      GetRanges,
      Match,
      MissingRange,
      MatchRanges,
      RangeSlice
    })
  );
  Object.assign(globalThis, decorate(YieldMap, { GetRanges }));

  let objects = commands
    .map(({ command }) => command.split(/\s+/g).find(a => /\.o/.test(a)))
    .map(o => 'build/x86_64-linux-debug/' + o);

  for(let line of PipeStream(['nm', '-A', ...objects])) {
    let fields = line.split(/:/);
    let [file, record] = fields;
    let type = record.slice(17, 18);
    let name = record.slice(19);
    let src = file.replace(/.*\.dir\/(.*)\.o$/g, '$1');

    if(/^\./.test(name)) continue;
    all.add(name);

    src2obj[src] ??= file;
    files[src] ??= [];
    files[src].push({ src, type, name });
  }

  globalThis.fileMap = new Map();

  globalThis.getFile = memoize(
    src =>
      lazyProperties(
        { code: std.loadFile(src), src },
        decorate([ThisAsArg, YieldAll], {
          comments: ({ code }) => GetComments(code),
          nonComments: ({ code, comments }) => MissingRange(comments, code.length),
          functions: ({ code, comments }) =>
            new Map(
              YieldAll(Match)(/^([a-zA-Z_][0-9a-zA-Z_]*)\(.*(,|{)$/gm, code)
                .filter(Negate(ArrayArgs(Within(comments))))
                .map(([index, name]) => [
                  name,
                  [code.lastIndexOf('\n', code.lastIndexOf('\n', index) - 1) + 1, code.indexOf('\n}', index) + 3]
                ])
            ),
          functionAt: ({ functions }) => i => { for(let [fn, [s, e]] of functions) if(i >= s && i < e) return fn; },
          functionByName: ({ functions, code }) => name => { for(let [fn, [s, e]] of functions) if(fn === name) return code.slice(s, e); },
          commentFunction: ({ slices }) => (name, s = slices.get(name)) => s.startsWith('/*') ? null : (slices.set(name, `/*${(s = slices.get(name))}*/`), s),
          slices: obj => new Map(GetRanges(obj.code, obj.functions.values(), (i, s) => [obj.functionAt(i) ?? i, s]))
        })
      ),
    fileMap
  );
  globalThis.allFiles = GetProxy(getFile, () => [...fileMap.keys()]);

  for(let file in files) {
    let records = files[file];
    let [def, undef] = (definitions[file] = SymbolsToDefinedUndefined(records));
    for(let record of records) {
      if(/^[A-TV-Z]$/.test(record.type)) lookup[record.name] ??= record;
    }
    for(let record of records) {
      if(/^[^U]$/.test(record.type)) lookup[record.name] ??= record;
    }
  }

  for(let file in definitions) {
    let [def, undef] = definitions[file];
    for(let name of undef)
      if(!(name in lookup)) {
        undef.delete(name);
        external.add(name);
      } else {
        used.add(name);
      }
  }

  let unused = new Set([...all].filter(k => !used.has(k) && !external.has(k)));
  let valid = new Set([...all].filter(n => n in lookup));

  for(let file in files) {
    let { code, functions } = getFile(file);
    // let fns = [...code.matchAll(/^([a-zA-Z_][0-9a-zA-Z_]*)\(.*(,|{)$/gm)];

    console.log(`Parsing functions '${file}'...`, functions);

    for(let [name, [s, e]] of functions) {
      /*let c = code.slice(s, e);
let fn=c.slice(n - s, c.indexOf('('));*/
      valid.add(name);
    }
  }

  /*  for(let file in files) {
    console.log(`Matching symbols '${file}'...`);

    let [undef, def] = MatchSymbols(getFile(file).code, valid);
    let exp = [];
    for(let f of getFunctionList(file).values()) exp.push(f);

    Object.assign(files[file], { undef, def, exp });
  }*/

  console.log('all', all.size);
  console.log('external', external.size);
  console.log('used', used.size);
  console.log('unused', unused.size);
  console.log('unused', console.config({ compact: false }), [...unused]);

  os.kill(process.pid, os.SIGUSR1);
}

function GetFunctionFromIndex(file, pos) {
  let funcName;
  for(let [index, fn] of getFunctionList(file)) {
    if(pos >= index) funcName = fn;
    if(pos < index) break;
  }
  return funcName;
}

function GetSymbol(name) {
  let record = lookup[name];
  return record;
}

function* MatchRanges(code, re) {
  for(let m of code.matchAll(re)) {
    let { index } = m;
    let [, match] = m;
    yield [index, index + match.length];
  }
}

function MatchSymbols(code, symbols) {
  let sl = [...symbols].join('|') || '[A-Za-z_][A-Za-z0-9_]*';
  // console.log('sl',sl);
  let re = new RegExp('(?:^|[^\n])(' + sl + ')', 'g');

  [...(code.matchAll(re) || [])].map(m => {
    let lineIndex = [code.lastIndexOf('\n', m.index) + 1, code.indexOf('\n', m.index + m[1].length)];
    let line = code.slice(...lineIndex);
    let columnPos = line.indexOf(m[1]);
    return [m.index, columnPos, m[1], code.slice(...lineIndex)];
  });
  return SplitByPred(
    matches,
    ([, column]) => column == 0,
    ([, , m]) => m
  );
}

function GetObj(symbol) {
  let record = lookup[symbol];
  return record;
}

function* PipeStream(command) {
  let [rd, wr] = os.pipe();
  let r = os.exec(command, { block: false, stdout: wr });
  os.close(wr);
  let line,
    out = std.fdopen(rd, 'r');
  while((line = out.getline()) !== null) yield line;
  out.close();
}

function SplitByPred(list, pred, t = a => a) {
  let sets = [[], []];
  for(let item of list) sets[0 | pred(item)].push(t(item));
  return sets;
}

function SymbolsToDefinedUndefined(symbols) {
  let list = symbols.filter(({ name }) => !/^\./.test(name));
  const pred = [({ type }) => type != 'U', ({ type }) => type == 'U'];
  return pred.map(p => new Set(list.filter(p).map(({ name }) => name)));
}

function ReadJSON(file) {
  let json = std.loadFile(file);
  return JSON.parse(json);
}

function* GetComments(code) {
  let i, n;
  for(i = 0, n = code.length; i < n; ) {
    let p = code.indexOf('/*', i);
    let e = code.indexOf('*/', p);
    if(p == -1 || e == -1) break;
    yield [p, e + 2];
    i = e + 2;
  }
}

function* MissingRange(ranges, length) {
  let i = 0;
  for(let [s, e] of ranges) {
    if(s > i) yield [i, s];
    i = e;
  }
  if(i < length) yield [i, length];
}

function* RangePoints(ranges) {
  let q;
  for(let r of ranges) {
    if(!Array.isArray(r)) r = [r];
    for(let p of r) {
      if(q !== p) yield p;
      q = p;
    }
  }
}

function* RangeSlice(points, length) {
  let i = 0;
  for(let p of RangePoints(points)) {
    if(p > i) yield [i, p];
    i = p;
  }
  if(i < length) yield [i, length];
}

function* GetRanges(s, ranges, t = (index, str) => [index, str]) {
  for(let range of RangeSlice(ranges, s.length)) yield t(range[0], s.slice(...range));
}

function YieldAll(g, thisObj) {
  if(typeof g == 'function')
    return function(...args) {
      let result = g.call(this, ...args);
      if('next' in result) result = [...result];
      return result;
    };

  if('next' in g) g = [...g];
  return g;
}

function YieldMap(g) {
  if(typeof g == 'function')
    return function(...args) {
      return new Map(g.call(this, ...args));
    };
  if('next' in g || Symbol.iterator in g) g = new Map(g);
  return g;
}

function ThisAsArg(g, thisObj) {
  if(typeof g == 'function')
    return function(...args) {
      return g.call(thisObj, this, ...args);
    };
}

function* Transform(g, t = a => a) {
  for(let item of g) yield t(item, g);
}

function* Match(re, s) {
  let match;
  if(typeof re != 'object' && !(re instanceof RegExp)) re = new RegExp(re, 'gm');
  while((match = re.exec(s))) yield [match.index, match[1]];
}

function InRange([s, e]) {
  return i => i >= s && i < e;
}

function Within(ranges) {
  return i => ranges.find(([s, e]) => i >= s && i < e);
}

function Negate(fn) {
  return function(...args) {
    return !fn.call(this, ...args);
  };
}

function ArrayArgs(fn) {
  return function(a) {
    return fn.call(this, ...a);
  };
}

function ArgsArray(fn) {
  return function(...a) {
    return fn.call(this, a);
  };
}

function Chain(...fns) {
  let ret;
  return (...args) => {
    let ret = args;
    for(let fn of fns) ret = fn(ret);
    return ret;
  };
}

function GetProxy(get, keys) {
  return new Proxy(
    {},
    {
      get: (target, prop, receiver) => get(prop),
      getOwnPropertyDescriptor: (target, prop) => ({ configurable: true, enumerable: true, value: get(prop) }),
      ownKeys: target => (keys ?? get)()
    }
  );
}

main(...scriptArgs.slice(1));
