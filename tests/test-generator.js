import { Generator } from 'net.so';
import { fail, assert, eq, assertEquals, assertStrictEquals, tests } from './tinytest.js';

import('console').then(({ Console }) => (globalThis.console = new Console({ inspectOptions: { compact: 2 } })));

async function main() {
  let gen = new Generator(async (push, stop) => {
    console.log('generator', { push, stop });
    for(let i = 0; i < 100; i++) {
      let pr = push(new Uint32Array([i * 1e6]));
      await pr;
      console.log(`wrote #${i}`);

      if(i >= 42) stop();
    }
    await stop(new Uint8Array([70, 105, 110, 105, 115, 104]));
  });
  console.log('gen', gen);

  /* for await(let item of gen) {
    console.log('item', new Uint32Array(item));
  }*/

  let item,
    i = 0;

  while((item = await gen.next())) {
    let { value, done } = item;
    console.log(`item #${i}`, { value, done });
    if(done) break;
  }
}

//main();

tests({
  async 'next() value'() {
    const gen = new Generator(async (push, stop) => push(await push(undefined)));

    await gen.next();
    eq((await gen.next('value#1')).value, 'value#1');
  },
  async 'await push'() {
    let step = 0;
    const gen = new Generator(async (push, stop) => {
      await push(step);
      step++;
      await push(step);
      step++;
      await push(step);
    });

    eq((await gen.next('value#1')).value, 0);
    eq(step, 0);
    eq((await gen.next('value#2')).value, 1);
    eq(step, 1);
    eq((await gen.next('value#3')).value, 2);
    eq(step, 2);
  },
  async 'return()'() {
    let step = 0;
    const gen = new Generator(async (push, stop) => {
      for(let i = 0; i < 10; i++) await push(i);
    });

    eq((await gen.next('value#1')).value, 0);
    eq((await gen.next('value#2')).value, 1);
    eq((await gen.return('value#3')).value, 'value#3');

    eq((await gen.next()).done, true);
    eq((await gen.next()).value, undefined);
  },
  async 'throw()+rethrow'() {
    const gen = new Generator(async (push, stop) => {
      for(let i = 0; i < 10; i++) await push(i);
    });

    eq((await gen.next('value#1')).value, 0);
    eq((await gen.next('value#2')).value, 1);

    try {
      await gen.throw(new Error('value#3'));
    } catch(err) {
      eq(err.message, 'value#3');

      eq((await gen.next()).done, true);
      eq((await gen.next()).value, undefined);
    }
  },
  async 'throw()+catch/stop()'() {
    const gen = new Generator(async (push, stop) => {
      try {
        for(let i = 0; i < 10; i++) await push(i);
      } catch(e) {
        stop(new Error('stop+' + e.message));
      }
    });

    eq((await gen.next('value#1')).value, 0);
    eq((await gen.next('value#2')).value, 1);

    try {
      let pr = gen.throw(new Error('value#3'));
      console.log('pr', pr);
      pr = await pr;
      console.log('pr', pr);
    } catch(err) {
      eq(err.message, 'stop+value#3');

      eq((await gen.next()).done, true);
      eq((await gen.next()).value, undefined);
    }
  },
  async 'throw()+handled'() {
    const gen = new Generator(async (push, stop) => {
      try {
        for(let i = 0; i < 10; i++) await push(i);
      } catch(e) {
        push(Infinity);
        stop();
      }
    });

    eq((await gen.next('value#1')).value, 0);
    eq((await gen.next('value#2')).value, 1);

    let r = await gen.throw(new Error('value#3'));
    eq(r.done, false);
    eq(r.value, Infinity);

    r = await gen.next();
    eq(r.done, true);
    eq(r.value, undefined);
  }
});
