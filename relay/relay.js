// Minimal command/reply relay for the board-app template.
//
// It's a per-board mailbox with two queues: commands (portal -> board) and replies
// (board -> portal). The board long-polls for commands and posts replies; the portal
// posts commands and long-polls for replies. No database, all in memory — fine for a
// single instance (one board, one operator). Zero dependencies: `node relay.js`.
//
//   POST /cmd/<id>     body = command text   -> queue a command for the board
//   GET  /pull/<id>    (board long-polls)    -> next command, or 204 after ~25s
//   POST /reply/<id>   body = reply text      -> queue a reply for the portal
//   GET  /poll/<id>    (portal long-polls)   -> next reply, or 204 after ~25s
//   GET  /health                             -> "ok"
//
// Auth: if RELAY_TOKEN is set, every request must send header  x-relay-token: <token>.
// CORS is wide open so the GitHub-Pages portal can reach it.

const http = require('http');

const PORT  = process.env.PORT || 8080;
const TOKEN = process.env.RELAY_TOKEN || '';   // empty = no auth (local testing)
const WAIT  = 25000;                            // long-poll hold time (ms)

const queues  = new Map();   // "cmd:<id>" / "reply:<id>" -> [text, ...]
const waiters = new Map();   // same key                  -> [ {res, timer}, ... ]

const CORS = {
  'Access-Control-Allow-Origin': '*',
  'Access-Control-Allow-Methods': 'GET,POST,OPTIONS',
  'Access-Control-Allow-Headers': 'content-type,x-relay-token',
};

const q = k => { if (!queues.has(k)) queues.set(k, []); return queues.get(k); };

const SEP = '\x1e';   // record separator between batched items (never appears in commands)
// Send EVERY queued item at once (joined by SEP) so a burst drains in one request instead
// of one-per-request — each request is an expensive fresh TLS handshake on the board.
function sendItem(key, res) {
  const items = q(key).splice(0);
  if (!items.length) { res.writeHead(204, CORS); res.end(); }
  else { res.writeHead(200, { ...CORS, 'Content-Type': 'text/plain' }); res.end(items.join(SEP)); }
}

// Enqueue and hand it straight to a parked long-poller if one is waiting.
function enqueue(key, text) {
  q(key).push(text);
  const list = waiters.get(key);
  if (list && list.length) { const w = list.shift(); clearTimeout(w.timer); sendItem(key, w.res); }
}

// Return queued items now, or park until one arrives / the timeout fires.
function longPoll(key, res) {
  if (q(key).length) return sendItem(key, res);
  const timer = setTimeout(() => {
    const list = waiters.get(key) || [];
    const i = list.findIndex(w => w.res === res); if (i >= 0) list.splice(i, 1);
    res.writeHead(204, CORS); res.end();
  }, WAIT);
  if (!waiters.has(key)) waiters.set(key, []);
  waiters.get(key).push({ res, timer });
}

const readBody = (req, cb) => { let b = ''; req.on('data', c => { b += c; if (b.length > 8192) req.destroy(); }); req.on('end', () => cb(b)); };

http.createServer((req, res) => {
  console.log(new Date().toISOString().slice(11, 19), req.method, req.url);   // watch board pulls + portal polls
  if (req.method === 'OPTIONS') { res.writeHead(204, CORS); return res.end(); }

  const parts = new URL(req.url, 'http://x').pathname.split('/').filter(Boolean);
  const [route, id] = parts;

  if (route === 'health') { res.writeHead(200, CORS); return res.end('ok'); }
  if (TOKEN && req.headers['x-relay-token'] !== TOKEN) { res.writeHead(401, CORS); return res.end('bad token'); }
  if (!id) { res.writeHead(404, CORS); return res.end('no id'); }

  if (route === 'cmd'   && req.method === 'POST') return readBody(req, t => { enqueue('cmd:' + id, t);   res.writeHead(200, CORS); res.end('queued'); });
  if (route === 'reply' && req.method === 'POST') return readBody(req, t => { enqueue('reply:' + id, t); res.writeHead(200, CORS); res.end('queued'); });
  if (route === 'pull'  && req.method === 'GET')  return longPoll('cmd:' + id,   res);
  if (route === 'poll'  && req.method === 'GET')  return longPoll('reply:' + id, res);

  res.writeHead(404, CORS); res.end('not found');
}).listen(PORT, () => console.log(`relay on :${PORT}${TOKEN ? ' (token required)' : ' (no auth)'}`));
