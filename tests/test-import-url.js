import { setLog, LLL_USER, fetch } from 'net.so';

async function main(...args) {
  setLog(LLL_USER, (level, msg) => console.log('LLL_USER', msg));

  moduleLoader(name => {
    if(/^https?:\/\//.test(name)) {
      const response = fetch(name);

      if(response) name = 'data:application/javascript;charset=utf-8,' + escape(response.text());
    }

    return name;
  });

  let module = await import(/*'https://google.ch/_/x.html' ?? */ 'https://esm.sh/stable/preact@10.16.0/es2022/preact.development.mjs');

  console.log('module', module);
  console.log('moduleList', console.config({ compact: false }), moduleList[moduleList.length - 1]);
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error && error.message}\n${error && error.stack}`);
}
