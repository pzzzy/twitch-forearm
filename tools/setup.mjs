// Local provisioning page. All Wi-Fi credentials stay in memory and a child
// process stdin pipe until delivered over USB. Never put them in argv/logs/files.
import http from 'node:http';
import {readFileSync} from 'node:fs';
import {spawn} from 'node:child_process';
import {randomBytes} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import path from 'node:path';

const here = path.dirname(fileURLToPath(import.meta.url));
const done = '<meta name="viewport" content="width=device-width"><h1>Configuration sent</h1><p>The display is connecting. Credentials were not saved on the Mac. You can close this tab.</p>';

export function parseSetupOptions(args) {
  const values = new Map();
  const known = new Set(['--port', '--session', '--channel', '--client-id', '--python', '--device-port']);
  for (let index = 0; index < args.length; index += 2) {
    if (!known.has(args[index]) || index + 1 >= args.length || values.has(args[index]))
      throw Error('Invalid setup option');
    values.set(args[index], args[index + 1]);
  }
  return {
    port: Number(values.get('--port') ?? '8487'),
    token: values.get('--session') ?? randomBytes(24).toString('base64url'),
    channel: values.get('--channel') ?? '',
    clientId: values.get('--client-id') ?? '',
    python: values.get('--python') ?? process.env.FOREARM_DEVICE_PYTHON ?? 'python3',
    devicePort: values.get('--device-port') ?? null,
  };
}

function validateOptions({port, token, channel, clientId, python, devicePort}) {
  if (!Number.isInteger(port) || port < 0 || port > 65535 ||
      !/^[A-Za-z0-9_-]{16,80}$/.test(token) ||
      (channel !== '' && !/^[A-Za-z0-9_]{1,25}$/.test(channel)) ||
      (clientId !== '' && !/^[A-Za-z0-9]{1,64}$/.test(clientId)) ||
      typeof python !== 'string' || !python || python.includes('\0') ||
      (devicePort !== null && (typeof devicePort !== 'string' || !devicePort ||
        devicePort.startsWith('-') || devicePort.includes('\0'))))
    throw Error('Invalid public setup parameters');
}

function send(res, status, body) {
  if (res.writableEnded) return;
  res.writeHead(status, {
    'Content-Type': 'text/html; charset=utf-8',
    'Cache-Control': 'no-store',
    'Content-Security-Policy': "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'",
    'Referrer-Policy': 'same-origin',
  });
  res.end(body);
}

// Export the handler factory so security tests can substitute the child process.
// Ordinary CLI operation still binds exclusively to the IPv4 loopback address.
export function createSetupServer(options, {spawnProcess = spawn, logger = console.log} = {}) {
  validateOptions(options);
  const {port, token, channel, clientId, python, devicePort} = options;
  const template = readFileSync(path.join(here, 'setup_form.html'), 'utf8')
    .replace('{{SESSION}}', token).replace('{{CHANNEL}}', channel).replace('{{CLIENT_ID}}', clientId);
  let submitted = false;
  let busy = false;
  const server = http.createServer(async (req, res) => {
    const boundPort = server.address()?.port ?? port;
    const host = `127.0.0.1:${boundPort}`;
    const origin = `http://${host}`;
    let url;
    try { url = new URL(req.url, origin); }
    catch { send(res, 400, 'Invalid request'); return; }
    if (req.headers.host !== host || url.origin !== origin || url.pathname !== '/' ||
        url.searchParams.get('session') !== token) {
      send(res, 404, 'Not found'); return;
    }
    if (req.method === 'GET') { send(res, 200, submitted ? done : template); return; }
    // Safari can send null/absent Origin after form navigation. Exact Host, the
    // unpredictable URL + form nonce and cross-site rejection remain mandatory.
    // A non-null Origin must still match this exact loopback origin.
    if (req.method !== 'POST' ||
        (req.headers.origin && req.headers.origin !== 'null' && req.headers.origin !== origin) ||
        req.headers['sec-fetch-site'] === 'cross-site') {
      send(res, 403, 'Invalid origin'); return;
    }
    let data = '';
    try {
      for await (const chunk of req) {
        data += chunk;
        if (Buffer.byteLength(data) > 4096) { send(res, 413, 'Request too large'); return; }
      }
    } catch { send(res, 400, 'Incomplete request'); return; }
    const fields = new URLSearchParams(data);
    data = '';
    if (fields.get('session') !== token) { send(res, 403, 'Invalid session'); return; }
    if (submitted) { send(res, 200, done); return; }
    if (busy) { send(res, 409, 'Setup is already being sent. Please wait.'); return; }
    const value = {
      ssid: fields.get('ssid') || '', password: fields.get('password') || '',
      client_id: fields.get('client_id') || '', channel: (fields.get('channel') || '').toLowerCase(),
    };
    if (!value.ssid || Buffer.byteLength(value.ssid) > 32 || Buffer.byteLength(value.password) > 63 ||
        !/^[A-Za-z0-9]{1,64}$/.test(value.client_id) || !/^[a-z0-9_]{1,25}$/.test(value.channel)) {
      send(res, 400, 'Invalid field. Use Back to correct it.'); return;
    }
    busy = true;
    const childArguments = [path.join(here, 'provision_stdin.py')];
    if (devicePort !== null) childArguments.push('--port', devicePort);
    let child;
    try { child = spawnProcess(python, childArguments, {cwd: here, stdio: ['pipe', 'ignore', 'ignore']}); }
    catch { busy = false; send(res, 503, 'Setup helper unavailable.'); return; }
    let finished = false;
    const finish = accepted => {
      if (finished) return;
      finished = true;
      clearTimeout(timeout);
      busy = false;
      if (accepted) {
        submitted = true;
        logger('Configuration accepted by display; credentials not logged.');
        send(res, 200, done);
      } else send(res, 503, 'Display is busy or unavailable. Use Back and retry after the USB test finishes.');
    };
    const timeout = setTimeout(() => { child.kill('SIGTERM'); finish(false); }, 15000);
    child.once('error', () => finish(false));
    child.once('close', code => finish(code === 0));
    child.stdin.once('error', () => finish(false));
    child.stdin.end(JSON.stringify(value));
    value.password = '';
  });
  return server;
}

function main() {
  if (process.argv.slice(2).includes('--help')) {
    console.log('Usage: node tools/setup.mjs [--port LISTEN_PORT] [--device-port USB_PORT] [--python PYTHON] [--channel CHANNEL] [--client-id PUBLIC_CLIENT_ID]');
    return;
  }
  const options = parseSetupOptions(process.argv.slice(2));
  if (options.port === 0) throw Error('Choose a listen port from 1 to 65535');
  const server = createSetupServer(options);
  server.listen(options.port, '127.0.0.1', () =>
    console.log(`Local setup: http://127.0.0.1:${options.port}/?session=${options.token}`));
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main();
