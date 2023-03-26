import * as std from 'std';
import * as os from 'os';
import { Console } from 'console';
import * as deep from 'deep';
import { memoize } from 'util';
globalThis.console = new Console({ inspectOptions: { compact: 2, maxArrayLength: Infinity, maxStringLength: 100 } });

function ReadJSON(file) {
  let json = std.loadFile(file);
  return JSON.parse(json);
}

let commands = ReadJSON('build/x86_64-linux-debug/compile_commands.json');

let objects = commands
  .map(({ command }) => command.split(/\s+/g).find(a => /\.o/.test(a)))
  .map(o => 'build/x86_64-linux-debug/' + o);

let src2obj = {};
let files = {},
  all = new Set();

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
let definitions = {},
  lookup = {},
  used = new Set(),
  external = new Set();
let getCode = memoize(src => [...PipeStream(['strip-comments', src])].join('\n'));

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

let valid = [...all].filter(n => n in lookup);
let getFunctionList = memoize(function (file) {
  let code = getCode(file);
  let fns = [...code.matchAll(/^([a-zA-Z_][0-9a-zA-Z_]*)\(.*(,|{)$/gm)];
  return new Map(fns.map(m => [m.index, m[1]]));
});

function GetFunctionFromIndex(file, pos) {
  let funcName;
  for(let [index, fn] of getFunctionList(file)) {
    if(pos >= index) funcName = fn;
    if(pos < index) break;
  }
  return funcName;
}

for(let file in files) {
  let code = getCode(file);
  let fns = [...code.matchAll(/^([a-zA-Z_][0-9a-zA-Z_]*)\(.*(,|{)$/gm)];

  let functionList = getFunctionList(file);
  console.log(`functionList ${file}`, functionList);
  let [undef, def] = MatchSymbols(code, valid);

  Object.assign(files[file], { undef, def });
}

console.log('all', all.size);
console.log('external', external.size);
console.log('used', used.size);
console.log('unused', unused.size);
console.log('unused', console.config({ compact: false }), [...unused]);
let paths = deep.select(files, v => v == 'minnet_url_constructor', deep.RETURN_PATH);

console.log('paths', paths);

function GetSymbol(name) {
  let record = lookup[name];
  return record;
}

function MatchSymbols(code, symbols) {
  let re = new RegExp('(?:^|[^\n])(' + [...symbols].join('|') + ')', 'g');
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
  let sets = [new Set(), new Set()];
  for(let item of list) sets[0 | pred(item)].add(t(item));
  return sets;
}

function SymbolsToDefinedUndefined(symbols) {
  let list = symbols.filter(({ name }) => !/^\./.test(name));
  const pred = [({ type }) => type != 'U', ({ type }) => type == 'U'];
  return pred.map(p => new Set(list.filter(p).map(({ name }) => name)));
}
