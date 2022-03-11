import { exit, puts } from 'std';
import { fetch } from 'net';

function FetchNext(array) {
  return new Promise((resolve, reject) => {
    let url = array.shift();
    console.log('fetching', url);
    fetch(url)
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

  FetchNext(args);
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error && error.message}\n${error && error.stack}`);
  exit(1);
}
