import { Generator } from 'net.so';

async function main() {
  import('console').then(({ Console }) => (globalThis.console = new Console({ inspectOptions: { compact: 2 } })));

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
  }
}

main();
