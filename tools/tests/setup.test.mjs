import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import {EventEmitter, once} from 'node:events';
import {createSetupServer, parseSetupOptions} from '../setup.mjs';

const token = 'host_tests_session_nonce_123456789';
const fields = {
  session: token, ssid: 'fixture-network', password: 'fixture-password',
  client_id: 'PublicClient123', channel: 'Stream_Test',
};

async function fixture(context, overrides = {}, behavior = 'success') {
  const calls = [];
  const logs = [];
  const spawnProcess = (python, args, options) => {
    if (behavior === 'throw') throw Error('mock child unavailable');
    const child = new EventEmitter();
    child.stdin = new EventEmitter();
    child.stdin.end = payload => {
      calls.push({python, args, options, payload});
      if (behavior === 'pipe-error') setImmediate(() => child.stdin.emit('error', Error('mock pipe error')));
      else setImmediate(() => child.emit('close', behavior === 'success' ? 0 : 1));
    };
    child.kill = () => child.emit('close', 143);
    return child;
  };
  const server = createSetupServer({
    ...parseSetupOptions([]), port: 0, token, python: 'mock-python', ...overrides,
  }, {spawnProcess, logger: line => logs.push(line)});
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  context.after(() => new Promise(resolve => server.close(resolve)));
  const port = server.address().port;
  const origin = `http://127.0.0.1:${port}`;
  const request = (method = 'GET', {headers = {}, path = `/?session=${token}`, body = ''} = {}) =>
    new Promise((resolve, reject) => {
      const wire = http.request({hostname: '127.0.0.1', port, method, path, agent: false,
        headers: {Connection: 'close', ...headers}}, response => {
        let data = '';
        response.setEncoding('utf8');
        response.on('data', chunk => { data += chunk; });
        response.on('end', () => resolve({status: response.statusCode, headers: response.headers, body: data}));
      });
      wire.on('error', reject);
      wire.end(body);
    });
  const post = (value = fields, headers = {}) => request('POST', {
    body: new URLSearchParams(value).toString(),
    headers: {'Content-Type': 'application/x-www-form-urlencoded', Origin: origin, ...headers},
  });
  return {calls, logs, request, post, origin, port};
}

test('blank public defaults render required fields without an owner-specific account', async context => {
  const options = parseSetupOptions([]);
  assert.equal(options.channel, '');
  assert.equal(options.clientId, '');
  assert.equal(options.devicePort, null);
  const f = await fixture(context);
  const response = await f.request();
  assert.equal(response.status, 200);
  assert.match(response.body, /name="channel" value=""[^>]*required/);
  assert.match(response.body, /name="client_id" value=""[^>]*required/);
  assert.match(response.headers['content-security-policy'], /form-action 'self'/);
  assert.match(response.headers['content-security-policy'], /frame-ancestors 'none'/);
  assert.equal(response.headers['cache-control'], 'no-store');
  assert.equal(f.calls.length, 0);
});

test('GET requires the exact Host, origin-form URL, root path and URL nonce', async context => {
  const f = await fixture(context);
  for (const options of [
    {path: '/'}, {path: '/?session=wrong'}, {path: `/other?session=${token}`},
    {headers: {Host: `localhost:${f.port}`}},
    {headers: {Host: 'attacker.invalid'}},
    {path: `http://attacker.invalid/?session=${token}`},
  ]) assert.equal((await f.request('GET', options)).status, 404);
  assert.equal(f.calls.length, 0);
});

test('POST rejects foreign Origin, cross-site fetches and missing/wrong form nonce', async context => {
  const f = await fixture(context);
  assert.equal((await f.post(fields, {Origin: 'https://attacker.invalid'})).status, 403);
  assert.equal((await f.post(fields, {'Sec-Fetch-Site': 'cross-site'})).status, 403);
  assert.equal((await f.post({...fields, session: ''})).status, 403);
  assert.equal((await f.post({...fields, session: 'wrong'})).status, 403);
  assert.equal((await f.request('DELETE')).status, 403);
  assert.equal(f.calls.length, 0);
});

test('POST still requires the URL nonce independently of the form nonce', async context => {
  const f = await fixture(context);
  const response = await f.request('POST', {path: '/', headers: {Origin: f.origin},
    body: new URLSearchParams(fields).toString()});
  assert.equal(response.status, 404);
  assert.equal(f.calls.length, 0);
});

test('empty Client ID/channel and invalid UTF-8 byte lengths do not start a child', async context => {
  const f = await fixture(context);
  for (const value of [
    {...fields, client_id: ''}, {...fields, channel: ''},
    {...fields, client_id: '<script>'}, {...fields, channel: '#stream'},
    {...fields, ssid: 'é'.repeat(17)}, {...fields, password: 'x'.repeat(64)},
  ]) assert.equal((await f.post(value)).status, 400);
  assert.equal(f.calls.length, 0);
});

test('oversized request is rejected without provisioning', async context => {
  const f = await fixture(context);
  const response = await f.request('POST', {headers: {Origin: f.origin}, body: 'x'.repeat(4097)});
  assert.equal(response.status, 413);
  assert.equal(f.calls.length, 0);
});

test('accepted fields stay in stdin and only the explicit public port enters child argv', async context => {
  const f = await fixture(context, {devicePort: '/dev/cu.fixture'});
  assert.equal((await f.post()).status, 200);
  assert.equal(f.calls.length, 1);
  const call = f.calls[0];
  assert.equal(call.python, 'mock-python');
  assert.match(call.args[0], /provision_stdin\.py$/);
  assert.deepEqual(call.args.slice(1), ['--port', '/dev/cu.fixture']);
  assert.deepEqual(call.options.stdio, ['pipe', 'ignore', 'ignore']);
  assert.deepEqual(JSON.parse(call.payload), {
    ssid: fields.ssid, password: fields.password, client_id: fields.client_id, channel: 'stream_test',
  });
  for (const value of [fields.ssid, fields.password, fields.client_id, fields.channel]) {
    assert.ok(!call.args.join(' ').includes(value));
    assert.ok(!f.logs.join(' ').includes(value));
  }
  assert.equal((await f.post()).status, 200);
  assert.equal(f.calls.length, 1, 'a submitted form must not provision twice');
});

test('automatic port selection stays in the provision helper without extra child argv', async context => {
  const f = await fixture(context);
  assert.equal((await f.post()).status, 200);
  assert.equal(f.calls[0].args.length, 1);
});

for (const originMode of ['null', 'absent']) {
  test(`Safari-compatible ${originMode} Origin still requires both nonces`, async context => {
    const f = await fixture(context);
    const headers = originMode === 'null' ? {Origin: 'null'} : {};
    const request = value => f.request('POST', {headers, body: new URLSearchParams(value).toString()});
    assert.equal((await request({...fields, session: 'wrong'})).status, 403);
    assert.equal(f.calls.length, 0);
    assert.equal((await request(fields)).status, 200);
    assert.equal(f.calls.length, 1);
  });
}

for (const behavior of ['throw', 'failure', 'pipe-error']) {
  test(`mock provisioning ${behavior} returns a generic error without echoing fields`, async context => {
    const f = await fixture(context, {}, behavior);
    const response = await f.post();
    assert.equal(response.status, 503);
    assert.ok(!response.body.includes(fields.password));
  });
}
