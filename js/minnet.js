import { FormParser, Generator, Hash, LLL_ALL, LLL_CLIENT, LLL_DEBUG, LLL_ERR, LLL_EXT, LLL_HEADER, LLL_INFO, LLL_LATENCY, LLL_NOTICE, LLL_PARSER, LLL_THREAD, LLL_USER, LLL_WARN, METHOD_DELETE, METHOD_GET, METHOD_HEAD, METHOD_OPTIONS, METHOD_PATCH, METHOD_POST, METHOD_PUT, Request, Response, Ringbuffer, Socket, URL, client, default, fetch, getSessions, logLevels, server, setLog } from 'net';


console.log('Request',Request);
delete Request.prototype.text;
Request.prototype.text = async function text() {}

export { FormParser, Generator, Hash, LLL_ALL, LLL_CLIENT, LLL_DEBUG, LLL_ERR, LLL_EXT, LLL_HEADER, LLL_INFO, LLL_LATENCY, LLL_NOTICE, LLL_PARSER, LLL_THREAD, LLL_USER, LLL_WARN, METHOD_DELETE, METHOD_GET, METHOD_HEAD, METHOD_OPTIONS, METHOD_PATCH, METHOD_POST, METHOD_PUT, Request, Response, Ringbuffer, Socket, URL, client, default, fetch, getSessions, logLevels, server, setLog } from 'net';
