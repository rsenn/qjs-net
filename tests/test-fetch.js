import { exit, puts, open } from 'std';
import { fetch, Request, Response, setLog, logLevels, LLL_DEBUG, LLL_INFO, LLL_USER } from 'net';
import { log } from './log.js';

function WriteFile(name, data) {
  try {
    let f = open(name, 'w+');
    log(`WriteFile \x1b[1;31m${name}\x1b[0m =`, f);

    let r = f.write(data, 0, data.byteLength);
    f.close();
    return r;
  } catch(err) {
    throw new Error(`Couldn't write to '${name}': ${err.message}`);
  }
}

function FetchNext(array) {
  return new Promise((resolve, reject) => {
    let url = array.shift();
    let reqObj;
    let request = new Request(
      url,
      (reqObj = {
        headers: {
          accept:
            'text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.9',
          'accept-language': 'en-US,en;q=0.9',
          pragma: 'no-cache',
          'cache-control': 'no-cache',
          authority: 'www.discogs.com',
          'user-agent':
            'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/99.0.4844.27 Safari/537.36',
          'upgrade-insecure-requests': '1',
          cookie:
            'sid=deb14330f89995598b4cd37ecd8f0c3d; language2=en; mp_session=ed5700f25fac3c643b872191; OptanonConsent=isIABGlobal=false&datestamp=Mon+Feb+14+2022+09%3A15%3A59+GMT%2B0100+(Central+European+Standard+Time)&version=6.20.0&hosts=&consentId=16f6b226-a0fd-429d-ba34-0bdad57d38f1&interactionCount=1&landingPath=https%3A%2F%2Fwww.discogs.com%2Fsell%2Fundefined&groups=C0001%3A1%2CC0004%3A1%2CC0003%3A1%2CC0002%3A1%2CSTACK8%3A0; currency=USD; ck_username=diskosenn; ppc_onboard_prompt=seen; session="5V0o/D1Lm2v3OYz32dQNvkTeAkE=?_expires=MTY2MjM3NDY4MQ==&auth_token=IktCZ0tWaWdxWkp3cWdubzZkY0RoMXpEb09EIg==&created_at=IjIwMjItMDMtMDlUMTA6NDQ6NDEuMjc3MDkxIg==&idp%3Auser_id=ODM2OTAyMg=="; __cf_bm=wo1vbcsHdRLdP1d.0TGEZ4nNZ4CrE_3K2j6Emuboaa8-1647067526-0-AVL1/sHkoRhF7/QIXuMC5nsTWQGo9HeFvV+unN2AzKdpRYx75fgQcO+o/8mqWVuP+CFBzCoVX+iGaQW2z3edfNs='
        }
      })
    );
    log(`fetching \x1b[1;33m${url}\x1b[0m`);
    log(console.config({ compact: 0 }), 'request:', request);
    fetch(url, reqObj)
      .then(response => {
        console.log('response', response);
        log(console.config({ compact: 0 }), response);
        let prom = response.arrayBuffer();
        prom.then(buf => {
          console.log('buf', buf);
          let prom = response.text();
          console.log('prom', prom);
          prom.then(text => {
            log('arrayBuffer()', console.config({ compact: 2 }), buf);

            let filename = response.url.path.replace(/.*\//g, '');

            log('filename', filename);
            WriteFile(filename, buf);

            array.length ? FetchNext(array) : resolve();
          });
        });
      })
      .catch(error => (log('error', error), reject(error)));
  });
}

function main(...args) {
  if(args.length == 0)
    //args = ['http://www.w3.org/Home.html'];
    args =
      'Assis,Autoren,Backgrounds,Beklagenswert,BewegungMaterie,BodyMassIndex,BriefBenz,CERN_Auffassungen,DeadEndBigBang,Elementarteilchen,GOMAntwort,GOMProjekt,Glaube,Gravitation,Hintergruende,Hix,Jooss,Kapillare,Krueger,LetterBenz,MasseEnergieFehler1,Materie,Materiedefinition,NeuesCERN,Neutrinos,Nobelpreis,PM_Urknall,PhysikFehler,Physik_heute,PhysikerPhysik,PistorPohl,SackgasseUrknall,Tegmark,TheoriePraxis,Urknall,Urknallbeschreibung,WasIstLos,Weltraumteleskop,WhatIsGoing,WikipediaPhysik_Einleitung'
        .split(',')
        .map(n => `http://hauptplatz.unipohl.de/Wissenschaft/${n}.htm`);

  setLog(
    -1,
    (() => {
      let lf = open('test-fetch.log', 'w');
      return (level, msg) => {
        //  log(logLevels[level].padEnd(10) + msg);
        lf.puts(logLevels[level].padEnd(10) + msg + '\n');
        lf.flush();
      };
    })()
  );

  import('console')
    .then(
      ({ Console }) => (
        (globalThis.console = new Console({
          inspectOptions: { compact: 1, depth: 2, maxArrayLength: 10, maxStringLength: 64, reparseable: false }
        })),
        run()
      )
    )
    .catch(run);

  function run() {
    FetchNext(args)
      .then(() => {
        log('SUCCEEDED');
      })
      .catch(err => {
        log('FAILED:', typeof err, err);
      });
  }
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  exit(1);
}
