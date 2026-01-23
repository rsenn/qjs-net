import net from 'net';
// net.setLog(net.LLL_USER, (level, message) => (true ? console.log('minnet', message.replace(/\n/g, '\\n')) : undefined)); function request(url) { return net .client(url, { async onResponse(ws, resp) { console.log('Response for', resp.url); let chunks = []; for await(let data of resp.body) { chunks.push(data); } console.log('chunks received:', chunks.length);
//
//console.log( 'bytes received:', chunks.reduce((total, chunk) => total + chunk.length, 0) ); }, onClose(ws, ...args) { console.log('closed', ...args); } }) .then(client => console.log('client.socket.fd', client.socket.fd)); } request('https://github.com/rsenn?tab=repositories'); await request('https://github.com/topics/http2');
