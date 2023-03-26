import * as std from 'std';
import * as os from 'os';

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

  console.log('fields:', { file, type, name }); //{file,type,name});

  files[file] ??= [];

  files[file].push({ type, name });
}

let definitions = {};
console.log('files', files);

for(let file in files) {
  definitions[file] = SymbolsToDefinedUndefined(files[file]);
}
console.log('definitions', definitions);

function SymbolsToDefinedUndefined(symbols) {
  let list = symbols.filter(({ name }) => !/^\./.test(name));
  return [
    list.filter(({ type }) => type != 'U').map(({ name }) => name),
    list.filter(({ type }) => type == 'U').map(({ name }) => name)
  ];
}
