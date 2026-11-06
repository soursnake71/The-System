// Offline test: fakes Twilio + ElevenLabs, checks routing, signatures, memory and summaries.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'callbuddy-'));
Object.assign(process.env, {
  CALLBUDDY_DATA: tmp, TWILIO_AUTH_TOKEN: 'tok', TWILIO_ACCOUNT_SID: 'AC1', TWILIO_NUMBER: '+15550000000',
  ELEVENLABS_API_KEY: 'k', ELEVENLABS_AGENT_ID: 'agent1', ELEVENLABS_WEBHOOK_SECRET: 'whsec',
  MY_CELL: '+15551112222', CALLER_NAME: 'Pat', CALLER_NUMBERS: '+1 (555) 333-4444',
  PUBLIC_URL: 'https://cb.example', ADMIN_KEY: 'adm', MODE: 'ai', TZ: 'America/New_York',
});

const calls = [];
globalThis.fetch = async (url, init) => {
  calls.push({ url: String(url), init });
  if (String(url).includes('register-call')) return new Response('<Response><Connect><Stream url="wss://el"/></Connect></Response>');
  return new Response('{}');
};

const { server } = await import('./server.mjs');
await new Promise((r) => server.listen(0, r));
const base = `http://127.0.0.1:${server.address().port}`;

const sign = (p, params) => crypto.createHmac('sha1', 'tok')
  .update('https://cb.example' + p + Object.keys(params).sort().map((k) => k + params[k]).join('')).digest('base64');
// fetch is mocked for outbound calls; use the real one for our own server
const realFetch = (await import('node:http')).default;
async function req(method, url, body, headers = {}) {
  return new Promise((resolve, reject) => {
    const r = realFetch.request(url, { method, headers }, (res) => {
      let b = ''; res.on('data', (c) => (b += c)); res.on('end', () => resolve({ status: res.statusCode, text: b }));
    });
    r.on('error', reject); if (body) r.write(body); r.end();
  });
}
async function fetchLocal(url, init = {}) {
  const r = await req(init.method || 'GET', url, init.body?.toString(), init.headers);
  return { status: r.status, text: async () => r.text };
}
const post = (params, p = '/voice', sig = sign(p, params)) =>
  fetchLocal(base + p, { method: 'POST', headers: { 'x-twilio-signature': sig, 'content-type': 'application/x-www-form-urlencoded' }, body: new URLSearchParams(params) })
    .then(async (r) => ({ status: r.status, text: await r.text() }));

// 1. Forged request rejected
assert.equal((await post({ From: '+15553334444' }, '/voice', 'bogus')).status, 403);

// 2. Caller -> agent, with memory variables
let r = await post({ From: '+15553334444', To: '+15550000000' });
assert.match(r.text, /<Stream/);
const reg = JSON.parse(calls.find((c) => c.url.includes('register-call')).init.body);
assert.equal(reg.agent_id, 'agent1');
assert.equal(reg.conversation_initiation_client_data.dynamic_variables.earlier_today, 'none');

// 3. Someone else -> rings my cell
r = await post({ From: '+15559998888' });
assert.match(r.text, /<Dial[^>]*>\+15551112222<\/Dial>/);

// 4. Post-call webhook: logged, SMS sent, flags raised
const evt = JSON.stringify({ type: 'post_call_transcription', data: {
  agent_id: 'agent1', conversation_id: 'c1', metadata: { call_duration_secs: 240 },
  analysis: { transcript_summary: 'Talked about the garden and the dog.' },
  transcript: [{ role: 'user', message: 'Do I need to pay the bank?' }, { role: 'agent', message: "I'll take care of it." }],
} });
const t = Math.floor(Date.now() / 1000);
const hmac = crypto.createHmac('sha256', 'whsec').update(`${t}.${evt}`).digest('hex');
assert.equal((await req('POST', base + '/elevenlabs/post-call', evt, { 'elevenlabs-signature': `t=${t},v0=bad` })).status, 401);
assert.equal((await req('POST', base + '/elevenlabs/post-call', evt, { 'elevenlabs-signature': `t=${t},v0=${hmac}` })).status, 200);
await new Promise((res) => setTimeout(res, 50));
const sms = calls.find((c) => c.url.includes('Messages.json'));
assert.ok(sms, 'SMS sent');
const smsBody = new URLSearchParams(sms.init.body).get('Body');
assert.match(smsBody, /MONEY/);
assert.match(smsBody, /call #1 today \(4 min\)/);

// 5. Next call today gets the earlier summary as context
calls.length = 0;
await post({ From: '+15553334444', To: '+15550000000' });
const reg2 = JSON.parse(calls.find((c) => c.url.includes('register-call')).init.body);
assert.match(reg2.conversation_initiation_client_data.dynamic_variables.earlier_today, /garden/);

// 6. Mode switch: ring first, then agent if no answer
assert.equal((await req('GET', base + '/mode?key=wrong&set=pass')).status, 403);
assert.match((await req('GET', base + '/mode?key=adm&set=ring')).text, /mode: ring/);
r = await post({ From: '+15553334444' });
assert.match(r.text, /timeout="20" action="\/voice\/after-ring"/);
r = await post({ From: '+15553334444', DialCallStatus: 'no-answer' }, '/voice/after-ring');
assert.match(r.text, /<Stream/);
r = await post({ From: '+15553334444', DialCallStatus: 'completed' }, '/voice/after-ring');
assert.match(r.text, /<Hangup\/>/);

// 7. ElevenLabs down -> ring me instead of silence
await req('GET', base + '/mode?key=adm&set=ai');
globalThis.fetch = async () => new Response('down', { status: 500 });
r = await post({ From: '+15553334444' });
assert.match(r.text, /<Dial[^>]*>\+15551112222<\/Dial>/);

server.close();
fs.rmSync(tmp, { recursive: true, force: true });
console.log('All callbuddy tests passed.');
