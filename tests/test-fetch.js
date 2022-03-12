import { exit, puts } from 'std';
import { fetch, setLog, logLevels, LLL_INFO, LLL_USER } from 'net';

function FetchNext(array) {
  return new Promise((resolve, reject) => {
    let url = array.shift();
    console.log('fetching', url);
    let promise = fetch(url, {});

    console.log('FetchNext', promise);

    promise
      .then(response => {
        console.log('response', response);

        if(array.length) FetchNext(array);
        else resolve();
      })
      .catch(error => reject(error));
  });
}

function main(...args) {
  if(args.length == 0) args = ['https://www.w3.org/'];

  setLog(-1, (level, msg) => {
    //if(level < LLL_INFO || level == LLL_USER)
    console.log('LWS', logLevels[level].padEnd(10), msg);
  });

    let promise =FetchNext(args);
    console.log('promise', promise);

    promise.then(() => {
      console.log('SUCCEEDED');
    })
    .catch(() => {
      console.log('FAILED');
    });
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  exit(1);
}
