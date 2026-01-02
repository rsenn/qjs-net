import * as os from 'os';
import { decorate } from 'util';
import { define } from 'util';
import { getset } from 'util';
import { lazyProperties } from 'util';
import { memoize } from 'util';
import { Console } from 'console';
import * as std from 'std';
let src2obj = {};
let files = (globalThis.files = {}),
  all = (globalThis.all = new Set());
let references = (globalThis.references = {}),
  lookup = (globalThis.lookup = {}),
  used = (globalThis.used = new Set()),
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
    YieldJoin,
    YieldMap,
    Within,
    ArrayArgs,
    Negate,
    decorate,
    Chain,
    Transform,
    SaveSlices
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
  Object.assign(globalThis, decorate([YieldAll, YieldJoin], { fc: FilterComments }));

  let objects = commands.map(({ command }) => command.split(/\s+/g).find(a => /\.o/.test(a))).map(o => 'build/x86_64-linux-debug/' + o);

  for(let line of PipeStream(['nm', '-A', ...objects])) {
    let fields = line.split(/:/);
    let [file, record] = fields;
    let type = record.slice(17, 18);
    let name = record.slice(19);
    let src = file.replace(/.*\.dir\/(.*)\.o$/g, '$1');

    if(/^\./.test(name)) continue;
    // all.add(name);

    src2obj[src] ??= file;
    files[src] ??= [];
    files[src].push({ type, name });
  }

  globalThis.fileMap = new Map();

  lazyProperties(globalThis, {
    defines: () =>
      new Map(
        [...fileMap.values()]
          .map(file => [...file.functions].map(([fn, range]) => [fn, define({ count: 0, range }, { file })]))
          .flat()
          .sort()
        //          .map((n, s, e) => [n, [0, [s, e]]])
      ),
    unused: () =>
      [...defines]
        .filter(([k, { count }]) => count == 0)
        .map(([k]) => k)
        .sort()
  });

  globalThis.getFile = memoize(
    src =>
      lazyProperties(
        define({ src }, { code: std.loadFile(src), src }),
        decorate([ThisAsArg, YieldAll], {
          comments: ({ code }) => GetComments(code),
          nonComments: ({ code, comments }) => MissingRange(comments, code.length),
          functions: ({ code, comments }) =>
            new Map(
              YieldAll(Match)(/^([a-zA-Z_][0-9a-zA-Z_]*)\(.*(,|{)$/gm, code)
                .filter(Negate(ArrayArgs(Within(comments))))
                .map(([index, name]) => [name, [code.lastIndexOf('\n', code.lastIndexOf('\n', index) - 1) + 1, code.indexOf('\n}', index) + 3]])
            ),
          functionAt:
            ({ functions }) =>
            i => {
              for(let [fn, [s, e]] of functions) if(i >= s && i < e) return fn;
            },
          functionByName:
            ({ functions, code }) =>
            name => {
              for(let [fn, [s, e]] of functions) if(fn === name) return code.slice(s, e);
            },
          remove:
            ({ slices }) =>
            name => {
              let i;
              if((i = typeof name == 'number' ? name : slices.findIndex(([k, v]) => k === name)) != -1) return slices.splice(i, 1)[0];
            },
          save:
            ({ slices, src }) =>
            (to = src) =>
              SaveSlices(
                slices.map(([, s]) => s),
                to
              ),
          commentFunction: ({ slices }) => {
            let [get, set] = getset(slices);
            let clean = globalThis.fc || (s => [...FilterComments(s)].join(''));
            return (name, s = get(name)[1]) => (s.startsWith('/*') ? null : (set(name, `/*${(s = clean(get(name)[1]))}*/`), s));
          },
          slices: obj => /*new Map*/ GetRanges(obj.code, obj.functions.values(), (i, s) => [obj.functionAt(i) ?? i, s])
        })
      ),
    fileMap
  );
  globalThis.allFiles = GetProxy(getFile, () => [...fileMap.keys()]);

  for(let file in files) {
    let obj = getFile(file);
    for(let [fn, range] of obj.functions) {
      lookup[fn] = lookup[fn] || [file, range];

      all.add(fn);
    }
  }

  for(let file in files) {
    let exp = '\\b(' + [...all].join('|') + ')\\b';

    let { code, comments } = allFiles[file];
    let matches = YieldAll(Match)(new RegExp(exp, 'gm'), code)
      .map(([i, n]) => [i, n, code.lastIndexOf('\n', i) + 1])
      .map(([i, n, s]) => [i, n, i - s, code.slice(s, code.indexOf('\n', i))])
      .filter(([i, n, col, line]) => col > 0)
      .filter(Negate(ArrayArgs(Within(comments))))
      //.map(([,n]) =>n)
      .map(([i, n]) => [allFiles[file].functionAt(i), n, lookup[n]]);

    let undef = new Set(matches.map(([, n]) => n));

    references[file] = matches;

    for(let [, m] of matches) {
      let def;
      if((def = defines.get(m))) {
        ++def.count;
        //console.log('def', def);
      }
    }

    //  console.log(`references[${file}]`, console.config({ compact: 1 }), undef);
  }

  /*  for(let file in references) {
    let [def, undef] = references[file];
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
    console.log(`Parsing functions '${file}'...`, functions);

    for(let [name, [s, e]] of functions) {
      valid.add(name);
    }
  }

  console.log('all', all.size);
  console.log('external', external.size);
  console.log('used', used.size);
  console.log('unused', unused.size);
  console.log('unused', console.config({ compact: false }), [...unused]);
*/
  os.kill(process.pid, os.SIGUSR1);
}

function GetFunctionFromIndex(file, pos) {
  let funcName;
  for(let [fn, [index, end]] of allFiles[file].functions) if(pos >= index && pos < end) funcName = fn;
  return funcName;
}

function GetSymbol(name) {
  let [src, range] = lookup[name];
  return [src, range];
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

  let matches = [...(code.matchAll(re) || [])].map(m => {
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
  let [file] = lookup[symbol];
  return allFiles[file];
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

function* FilterComments(s) {
  let comments = GetComments(s);
  let ranges = GetRanges(s, comments);
  let isComment = i => comments.findIndex(([s, e]) => i == s) != -1;

  for(let [idx, s] of ranges) if(!s.startsWith('/*')) yield s;
}

function SaveSlices(g, file) {
  let f = std.open(file, 'w+');
  for(let chunk of g)
    if(typeof chunk == 'string') f.puts(chunk);
    else if(typeof chunk == 'object' && chunk instanceof ArrayBuffer) f.write(chunk, 0, chunk.byteLength);
    else throw new TypeError(`SaveSlices: chunk = ${chunk}`);
  f.flush();
  f.close();
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

function YieldJoin(g, s = '') {
  if(typeof g == 'function')
    return function(...args) {
      return g.call(this, ...args).join(s);
    };

  return g.call(this, ...args).join(s);
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