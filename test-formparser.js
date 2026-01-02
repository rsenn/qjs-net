import { createServer } from 'net.so';
import { FormParser } from 'net.so';
import { Generator } from 'net.so';
import { LLL_USER } from 'net.so';
import { Request } from 'net.so';
import { Response } from 'net.so';
import { setLog } from 'net.so';

const args = [...(globalThis.scriptArgs ?? process.argv)];

const host = args[1] ?? 'localhost',
  port = args[2] ? +args[2] : 30000;

//  setLog(LLL_USER, (level, message) => /(HTTP|WS)/.test(message) && console.log('LOG', message));

const log = (fnName, ...args) => console.log(`\x1b[1;33m${fnName}\x1b[0m`, ...args);

createServer({
  block: false,
  tls: true,
  host,
  port,
  protocol: 'http',
  onRequest(req, resp) {
    const ws = this;
    const { headers } = req;

    const type = headers.get('content-type');

    if(typeof type == 'string' && type.startsWith('multipart/form-data')) {
      console.log('MULTIPART!');

      globalThis.gen = new Generator(async (push, stop) => {
        new FormParser(
          ws,
          [
            /*'file', 'image'*/
          ],
          {
            chunkSize: 256 ** 3,
            onOpen(fp, name) {
              log('onOpen', name);
              this.name = name;
              this.data = new Generator(() => {} /*, 4096*/);

              const { data } = this;
              /*const params = this.params;
            log('onOpen', { params: params.map(n => [n, this[n]]) });*/

              push([name, data]);
            },
            onContent(fp, buf) {
              const { name, data } = this;
              log('onContent', buf);

              data.write(buf);

              /* const params = this.params;
            log('onContent', { params: params.map(n => [n, this[n]]) });*/
            },
            onClose(fp, name) {
              const { data } = this;
              log('onClose', name, data);

              data.stop();
            },
            onFinalize(fp) {
              const { name } = this;
              const resp = new Response('done', { status: 200 });

              log('onFinalize', resp);
              stop();

              return resp;
            },
          },
        );
      });

      (async function() {
        for await(let [name, data] of await gen) {
          console.log('Gen', name, data);

          for await(let chunk of data) {
            console.log('chunk', chunk);
          }
        }
      })();
    } else {
      console.log('onRequest', { ws, req, headers: Object.fromEntries(headers.entries()) });
    }
  },
});