import * as std from 'std';
import * as os from 'os';
import { Console } from 'console';
import * as deep from 'deep';

globalThis.console = new Console({ inspectOptions: { compact: 1, maxArrayLength: Infinity } });

function ReadJSON(file) {
  let json = std.loadFile(file);
  return JSON.parse(json);
}

let commands = ReadJSON('build/x86_64-linux-debug/compile_commands.json');

let objects = commands
  .map(({ command }) => command.split(/\s+/g).find(a => /\.o/.test(a)))
  .map(o => 'build/x86_64-linux-debug/' + o);

let [rd, wr] = os.pipe();
let r = os.exec(['nm', '-A', ...objects], { block: false, stdout: wr });

os.close(wr);

let line;
/*let ret,buf=new ArrayBuffer(1024);


while((ret=os.read(rd,buf, 0, 1024))) {
  console.log('ret:',ret);
}
*/
let files = {};

let out = std.fdopen(rd, 'r');

while((line = out.getline())) {
  let fields = line.split(/:/);
  let [file, record] = fields;

  let type = record.slice(17, 18);
  let name = record.slice(19);

  let src = file.replace(/.*\.dir\/(.*)\.o$/g, '$1');
  //  console.log('fields:', { file, type, name }); //{file,type,name});

  files[src] ??= [];

  files[src].push({ file, type, name });
}

let definitions = {},
  lookup = {},
  used = new Set(),
  external = new Set();
//console.log('files', files);

for(let file in files) {
  let [def, undef] = (definitions[file] = SymbolsToDefinedUndefined(files[file]));

  for(let name of def) lookup[name] = file;
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

let all = new Set(Object.keys(lookup));
let unused = new Set([...all].filter(k => !used.has(k)));

function GetObj(symbol) {
  let file = lookup[symbol];
  let record = files[file].find(rec => rec.name == symbol);
  return {file, ...record};
}

//console.log('definitions', definitions);
console.log('all', all.size);
console.log('external', external.size);
console.log('used', used.size);
console.log('unused', unused.size);
console.log('unused', [...unused].map(GetObj));


let paths=deep.select(files, v => v=='minnet_url_constructor', deep.RETURN_PATH);
console.log('paths', paths);

function SymbolsToDefinedUndefined(symbols) {
  let list = symbols.filter(({ name }) => !/^\./.test(name));
  const pred = [({ type }) => type != 'U', ({ type }) => type == 'U'];

  return pred.map(p => new Set(list.filter(p).map(({ name }) => name)));
}
