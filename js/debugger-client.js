import { Button, Panel } from './components.js';
import { DebuggerProtocol } from './debuggerprotocol.js';
import { classNames } from './lib/classNames.js';
import { JSLexer, Location } from './lib/jslexer.js';
import { WebSocketClient } from './lib/net/websocket-async.js';
import path from './lib/path.js';
import { trkl } from './lib/trkl.js';
import Util from './lib/util.js';
import { toString, toArrayBuffer, extendArray } from './lib/misc.js';
import { default as React, h, html, render, Fragment, Component, useState, useLayoutEffect, useRef } from './lib/dom/preactComponent.js';
import { EventEmitter, EventTarget } from './lib/events.js';
import { DroppingBuffer, FixedBuffer, MAX_QUEUE_LENGTH, Repeater, RepeaterOverflowError, SlidingBuffer } from './lib/repeater/repeater.js';
import { useAsyncIter, useRepeater, useResult, useValue } from './lib/repeater/react-hooks.js';
import { TimeoutError, delay, interval, timeout } from './lib/repeater/timers.js';
import { InMemoryPubSub } from './lib/repeater/pubsub.js';
import { semaphore, throttler } from './lib/repeater/limiters.js';
import { HSLA, RGBA, Point, isPoint, Size, isSize, Line, isLine, Rect, isRect, PointList, Polyline, Matrix, isMatrix, BBox, TRBL, Timer, Tree, Node, XPath, Element, isElement, CSS, SVG, Container, Layer, Renderer, Select, ElementPosProps, ElementRectProps, ElementRectProxy, ElementSizeProps, ElementWHProps, ElementXYProps, Align, Anchor, dom, isNumber, Unit, ScalarValue, ElementTransformation, CSSTransformSetters, Transition, TransitionList, RandomColor } from './lib/dom.js';
import { useIterable, useIterator, useAsyncGenerator, useAsyncIterable, useAsyncIterator, useGenerator, useActive, useClickout, useConditional, useDebouncedCallback, useDebounce, useDimensions, useDoubleClick, useElement, EventTracker, useEvent, useFocus, useForceUpdate, useGetSet, useHover, useMousePosition, useToggleButtonGroupState, useTrkl, useFetch, clamp, identity, noop, compose, maybe, snd, toPair, getOffset, getPositionOnElement, isChildOf } from './lib/hooks.js';
/* prettier-ignore */ 
/* prettier-ignore */ 
/* prettier-ignore */ 
/* prettier-ignore */ 
/* prettier-ignore */ 
/* prettier-ignore */ 
/* prettier-ignore */ 
/* prettier-ignore */ 
/* prettier-ignore */ 
/* prettier-ignore */ 
/* prettier-ignore */ 
let cwd = '.';
let responses = {};
let currentSource = trkl(null);
let currentLine = trkl(-1);
let url;
let seq = 0;

currentSource.id = 'currentSource';
currentLine.id = 'currentLine';

currentSource.subscribe(source => console.log('currentSource set to', source));
currentLine.subscribe(line => console.log('currentLine set to', line));

const doRender = Util.memoize(RenderUI);

window.addEventListener('load', e => {
  url = Util.parseURL();
  console.log('URL', url);
  let socketURL = Util.makeURL({
    location: url.location + '/ws',
    protocol: url.protocol == 'https' ? 'wss' : 'ws'
  });

  (async () => {
    globalThis.ws = await CreateSocket(socketURL);
    console.log(`Loaded`, { socketURL, ws });
  })();
});

globalThis.addEventListener('keypress', e => {
  const handler = {
    KeyN: Next,
    KeyI: StepIn,
    KeyO: StepOut,
    KeyC: Continue,
    KeyP: Pause
  }[e.code];
  //console.log('keypress', e, handler);

  if(handler) handler();
});

/******************************************************************************
 * Components                                                                 *
 ******************************************************************************/
const SourceLine = ({ lineno, text, active, children }) =>
  h(Fragment, {}, [
    h(
      'pre',
      {
        class: classNames('lineno', active && 'active', ['even', 'odd'][lineno % 2])
      },
      h('a', { name: `line-${lineno}` }, [lineno + ''])
    ),
    h('pre', { class: classNames('text', active && 'active'), innerHTML: text })
  ]);

const SourceText = ({ text, filename }) => {
  const activeLine = useTrkl(currentLine);
  let tokens, lines;

  try {
    tokens = TokenizeJS(text, filename);
    lines = [...tokens];
  } catch(e) {
    console.log('Error tokenizing:', e.message);
  }

  lines ??= text.split(/\n/g).map(line => [[null, line]]);

  return h(
    Fragment,
    {},
    lines.reduce((acc, tokens, i) => {
      const text = tokens
        .map(([type, token]) => [type, token.replace(/ /g, '\xa0')])
        .reduce((acc, [type, token]) => {
          acc.push(type == 'whitespace' ? token : `<span class="${type}">${token}</span>`);
          return acc;
        }, []);

      //console.log('text',text);
      acc.push(h(SourceLine, { lineno: i + 1, text: text.join(''), active: activeLine == i + 1 }, text));

      return acc;
    }, [])
  );
};

const SourceFile = props => {
  console.log('props.file', currentSource());
  const file = useTrkl(currentSource);
  console.log('file', file);
  const filename = file ? path.relative(cwd, file, cwd) : null;
  let text =
    (file &&
      !/^<.*>$/.test(file) &&
      useFetch(filename, resp => {
        console.log('Fetch', resp.status, Util.makeURL({ location: '/' + filename }));
        return resp.text();
      })) ||
    '';

  return h('div', { class: 'container' }, [
    h('div', {}, []),
    h('div', { class: 'header' }, [filename]),
    h(SourceText, { text, filename })
  ]);
};

/******************************************************************************
 * End of Components                                                          *
 ******************************************************************************/

async function LoadSource(filename) {
  try {
    let response = await fetch(filename);
    return await response.text();
  } catch(e) {}
}

/* prettier-ignore */ Object.assign(globalThis, { Connect,DebuggerProtocol, LoadSource, Util, toString, toArrayBuffer, extendArray, React, h, html, render, Fragment, Component, useState, useLayoutEffect, useRef, EventEmitter, EventTarget, Element, isElement, path });
/* prettier-ignore */ Object.assign(globalThis, { DroppingBuffer, FixedBuffer, MAX_QUEUE_LENGTH, Repeater, RepeaterOverflowError, SlidingBuffer, useAsyncIter, useRepeater, useResult, useValue, TimeoutError, delay, interval, timeout, InMemoryPubSub, semaphore, throttler, trkl });
/* prettier-ignore */ Object.assign(globalThis, { HSLA, RGBA, Point, isPoint, Size, isSize, Line, isLine, Rect, isRect, PointList, Polyline, Matrix, isMatrix, BBox, TRBL, Timer, Tree, Node, XPath, Element, isElement, CSS, SVG, Container, Layer, Renderer, Select, ElementPosProps, ElementRectProps, ElementRectProxy, ElementSizeProps, ElementWHProps, ElementXYProps, Align, Anchor, dom, isNumber, Unit, ScalarValue, ElementTransformation, CSSTransformSetters, Transition, TransitionList, RandomColor });
/* prettier-ignore */ Object.assign(globalThis, {   useIterable, useIterator, useAsyncGenerator, useAsyncIterable, useAsyncIterator, useGenerator, useActive, useClickout, useConditional, useDebouncedCallback, useDebounce, useDimensions, useDoubleClick, useElement, EventTracker, useEvent, useFocus, useForceUpdate, useGetSet, useHover, useMousePosition, useToggleButtonGroupState, useTrkl, useFetch });
/* prettier-ignore */ Object.assign(globalThis, { clamp, identity, noop, compose, maybe, snd, toPair, getOffset, getPositionOnElement, isChildOf });
/* prettier-ignore */ Object.assign(globalThis, { RenderUI, Start, GetVariables, SendRequest, StepIn,StepOut, Next, Continue, Pause, Evaluate, StackTrace });
/* prettier-ignore */ Object.assign(globalThis, {JSLexer, Location});

function Start(args, address) {
  return Initiate('start', address, false, args);
}

function Connect(address) {
  return Initiate('connect', address, true);
}

function Initiate(command, address, connect = false, args) {
  address ??= `${url.query.address ?? '127.0.0.1'}:${url.query.port ?? 9901}`;
  console.log('Initiate', { command, address, connect, args });
  return ws.send(JSON.stringify({ command, connect, address, args }));
}

const tokenColors = {
  comment: new RGBA(0, 255, 0),
  regexpLiteral: new RGBA(255, 0, 255),
  templateLiteral: new RGBA(0, 255, 255),
  punctuator: new RGBA(0, 255, 255),
  numericLiteral: new RGBA(0, 255, 255),
  stringLiteral: new RGBA(0, 255, 255),
  booleanLiteral: new RGBA(255, 255, 0),
  nullLiteral: new RGBA(255, 0, 255),
  keyword: new RGBA(255, 0, 0),
  identifier: new RGBA(255, 255, 0),
  privateIdentifier: new RGBA(255, 255, 0),
  whitespace: new RGBA(255, 255, 255)
};

function* TokenizeJS(data, filename) {
  let lex = new JSLexer();
  lex.setInput(data, filename);

  let { tokens } = lex;
  let colors = Object.entries(tokenColors).reduce(
    (acc, [type, c]) => ({ ...acc, [tokens.indexOf(type) + 1]: c.hex() }),
    {}
  );
  let prev = {};
  let out = [];
  for(let { id, lexeme, line } of lex) {
    const type = tokens[id - 1];
    let { line } = lex.loc;
    line -= lexeme.split(/\n/g).length - 1;

    //console.log('tok', { id, lexeme, line });

    if(prev.line != line) {
      for(let i = prev.line; i < line; i++) {
        yield out;
        out = [];
      }
    }

    for(let s of lexeme.split(/\n/g).reduce((acc, l) => {
      if(l != '') {
        if(acc.length) acc[acc.length - 1] += '\n';
        acc.push(l);
      }
      return acc;
    }, [])) {
      out.push([type, s]);
      if(s.endsWith('\n')) {
        yield out;
        out = [];
        line++;
      }
    }

    prev.line = line;
    line = lex.loc;
  }
  out += '</pre>';
}

Object.assign(globalThis, {
  responses,
  currentLine,
  currentSource,
  TokenizeJS
});
Object.assign(globalThis, { Start, Initiate, LoadSource, GetVariables });

async function CreateSocket(endpoint) {
  let ws = (globalThis.ws = new WebSocketClient());

  console.log('ws', ws);
  await ws.connect(endpoint);

  (async function ReadSocket() {
    for await(let msg of ws) {
      let data;
      try {
        data = JSON.parse(msg.data);
      } catch(e) {
        console.log('WS ERROR parsing', msg.data);
      }
      globalThis.response = data;
      if(data) {
        console.log('ws received ', data);
        const { response, request_seq } = data;
        if(response) {
          const { command } = response;

          /* if(command == 'file') {
            const { path, data } = response;
            CreateSource(data, path);
            continue;
          } else*/

          if(['start', 'connect'].indexOf(command) >= 0) {
            cwd = response.cwd;

            console.log('command:', command);
            console.log('response:', response);

            if(response.args[0]) {
              currentSource(response.args[0]);
            } else {
              UpdatePosition();
            }
            RenderUI();
            continue;
          }

          if(command == 'start') {
            cwd = response.cwd;
            console.log('start', response);
            RenderUI(response.args[0]);
            continue;
          }
        }

        if(responses[request_seq]) responses[request_seq](data);
      } else {
        console.log('WS', data);
      }
      if(['end', 'error'].indexOf(data.type) >= 0) {
        document.body.innerHTML = '';
        continue;
      }
    }
  })();

  ws.sendMessage = function(msg) {
    return this.send(JSON.stringify(msg));
  };

  if(url.query.port) await Connect();
  else await Start([url.query.script ?? 'test-video.js', 'test.jpg']);

  return ws;
}

function GetVariables(ref = 0) {
  return SendRequest('variables', { variablesReference: ref });
}

async function UpdatePosition() {
  const stack = (globalThis.stack = await StackTrace());
  console.log('stack', stack);

  const { filename, line, name } = stack[0];

  currentSource(filename);
  //  RenderUI(filename);
  currentLine(line);

  RenderUI();

  // doRender(currentSource);

  window.location.hash = `#line-${line}`;
}

async function StepIn() {
  await SendRequest('stepIn');
  await UpdatePosition();
}

async function StepOut() {
  await SendRequest('stepOut');
  await UpdatePosition();
}

async function Next() {
  await SendRequest('next');
  await UpdatePosition();
}

async function Continue() {
  return SendRequest('continue');
}

async function Pause() {
  await SendRequest('pause');
  await UpdatePosition();
}

async function Evaluate(expression) {
  return SendRequest('evaluate', { expression });
}

async function StackTrace() {
  let { body } = await SendRequest('stackTrace');
  return body;
}

/*
  {
    "type": "breakpoints",
    "breakpoints": {
      "path": "lib/ecmascript/parser2.js",
      "breakpoints": [ { "line": 470, "column": 0 }, { "line": 2151, "column": 0 }, { "line": 2401, "column": 0 } ]
    }
  }
*/

function SendRequest(command, args = {}) {
  const request_seq = ++seq;
  ws.sendMessage({ type: 'request', request: { request_seq, command, args } });
  return new Promise((resolve, reject) => (responses[request_seq] = resolve));
}

/*const Button = ({image}) => {
const ref = useClick(e => {
  console.log('click!!!!');
});
 return  h('button', { ref, class: 'button' }, h('img', { src: image }));
}*/
/*

const ButtonBar=  ({children}) => 
h('div', {class: 'button-bar' }, children);*/

function RenderUI() {
  console.log('RenderUI');
  /* if(currentSource() != file) 
    currentSource(file);*/

  const component = h(Fragment, {}, [
    h(Panel, { className: classNames('buttons', 'no-select'), tag: 'header' }, [
      h(Button, { image: 'static/svg/continue.svg', fn: Continue }),
      h(Button, { image: 'static/svg/pause.svg', fn: Pause }),
      //h(Button, {image: 'static/svg/start.svg'}),
      h(Button, {
        image: 'static/svg/step-into.svg',
        fn: StepIn
      }),
      h(Button, {
        image: 'static/svg/step-out.svg',
        fn: StepOut
      }),
      h(Button, { image: 'static/svg/step-over.svg', fn: Next })
      //   h(Button, { image: 'static/svg/restart.svg' }),
      //h(Button, {image: 'static/svg/stop.svg', enable: trkl(false)}),
    ]),
    h('main', {}, h(SourceFile, { file: currentSource })),
    h('footer', {}, [])
  ]);
  const { body } = document;
  let r = render(component, body);
  console.log('rendered', r);
}