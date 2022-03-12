import { exit, puts, open } from 'std';
import { fetch, Request, Response, setLog, logLevels, LLL_INFO, LLL_USER } from 'net';

function FetchNext(array) {
  return new Promise((resolve, reject) => {
    let url = array.shift();
    let request = new Request(url);
    console.log(`fetching \x1b[1;33m${url}\x1b[0m`);
    console.log(request);
    let promise = fetch(request, {});
    promise
      .then(response => {
        console.log(response);
        let prom = response.arrayBuffer();
        prom.then(buf => {
          let prom = response.text();
          prom.then(text => {
            console.log('arrayBuffer()', console.config({ compact: 2 }), buf);
            //console.log('text()', text);
            array.length ? FetchNext(array) : resolve();
          });
        });
      })
      .catch(error => (console.log('error', error), reject(error)));
  });
}

function main(...args) {
  if(args.length == 0)
    //args = ['http://www.w3.org/Home.html'];
    args = 'Assis,Autoren,Backgrounds,Beklagenswert,BewegungMaterie,BodyMassIndex,BriefBenz,CERN_Auffassungen,DeadEndBigBang,Elementarteilchen,GOMAntwort,GOMProjekt,Glaube,Gravitation,Hintergruende,Hix,Jooss,Kapillare,Krueger,LetterBenz,MasseEnergieFehler1,Materie,Materiedefinition,NeuesCERN,Neutrinos,Nobelpreis,PM_Urknall,PhysikFehler,Physik_heute,PhysikerPhysik,PistorPohl,SackgasseUrknall,Tegmark,TheoriePraxis,Urknall,Urknallbeschreibung,WasIstLos,Weltraumteleskop,WhatIsGoing,WikipediaPhysik_Einleitung'.split(',').map(n => `http://hauptplatz.unipohl.de/Wissenschaft/${n}.htm`);

  let log = std.open('test-fetch.log', 'w+');

  setLog(-1, (level, msg) => {
    log.puts(logLevels[level].padEnd(10) + msg + '\n');
    log.flush();
  });

  import('console')
    .then(({ Console }) => ((globalThis.console = new Console({ inspectOptions: { compact: 1, depth: 2, maxArrayLength: 10, maxStringLength: 30, reparseable: false } })), run()))
    .catch(run);

  function run() {
    let promise = FetchNext(args);
    console.log('promise', promise);
    promise
      .then(() => {
        console.log('SUCCEEDED');
      })
      .catch(err => {
        console.log('FAILED:', typeof err, err);
      });
  }
}
try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  exit(1);
}
