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
const http = (await import('node:http')).default;
const realFetch = http;
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
assert.match(r.text, /timeout="20" action="\/voice\/after-ring\?next=agent"/);
// you decline -> carrier forwards it back -> treated as busy, not a loop
assert.match((await post({ From: '+15553334444' })).text, /<Reject reason="busy"\/>/);
r = await post({ From: '+15553334444', DialCallStatus: 'busy' }, '/voice/after-ring?next=agent');
assert.match(r.text, /<Stream/);
r = await post({ From: '+15553334444', DialCallStatus: 'completed' }, '/voice/after-ring?next=agent');
assert.match(r.text, /<Hangup\/>/);
// strangers who aren't answered get voicemail, and you get a text
r = await post({ From: '+15557776666', DialCallStatus: 'no-answer' }, '/voice/after-ring?next=voicemail');
assert.match(r.text, /<Record/);
calls.length = 0;
await post({ From: '+15557776666', RecordingUrl: 'https://api.twilio.com/rec/RE1', RecordingDuration: '9' }, '/voice/recorded');
assert.match(new URLSearchParams(calls.find((c) => c.url.includes('Messages.json')).init.body).get('Body'), /Voicemail from \+15557776666/);
// never route a call from our own Twilio number
assert.match((await post({ From: '+15550000000' })).text, /<Reject\/>/);

// 7. ElevenLabs down -> ring me (then voicemail) instead of silence
await req('GET', base + '/mode?key=adm&set=ai');
globalThis.fetch = async () => new Response('down', { status: 500 });
r = await post({ From: '+15553334444', DialCallStatus: 'busy' }, '/voice/after-ring?next=agent');
assert.match(r.text, /<Dial[^>]*next=voicemail[^>]*>\+15551112222<\/Dial>/);

// 8. iPhone app + its API
globalThis.fetch = async (url, init) => {
  calls.push({ url: String(url), init });
  if (String(url).endsWith('/v1/voices/add')) return new Response('{"voice_id":"v123"}');
  if (String(url).includes('/v1/convai/agents')) return new Response('{"agent_id":"agent1"}');
  if (String(url).includes('/text-to-speech/')) return new Response(new Uint8Array(3000), { headers: { 'content-type': 'audio/mpeg' } });
  return new Response('{}');
};
Object.assign(process.env, { MY_NAME: 'Chris', CALLER_NAME: 'Pat', ELEVENLABS_AGENT_ID: '' });
const K = { 'x-admin-key': 'adm' };
const api = async (m, p, body, h = {}) => {
  const r = await req(m, base + p, body, { ...K, ...h });
  return { status: r.status, j: r.text.startsWith('{') ? JSON.parse(r.text) : r.text };
};
assert.match((await req('GET', base + '/')).text, /<title>Call Buddy<\/title>/);
assert.equal((await req('GET', base + '/api/status')).status, 403);
let s = (await api('GET', '/api/status')).j;
assert.equal(s.mode, 'ai'); assert.equal(s.samples, 0); assert.deepEqual(s.callerNumbers, ['5553334444']); assert.ok(s.profileIsExample);
assert.equal(s.urls.voice, 'https://cb.example/voice');
assert.equal((await api('POST', '/api/clone')).status, 400, 'no samples -> error');
assert.equal((await api('POST', '/api/samples', Buffer.alloc(100), { 'content-type': 'audio/mp4' })).status, 400, 'too short');
s = (await api('POST', '/api/samples', Buffer.alloc(50000), { 'content-type': 'audio/mp4' })).j;
assert.equal(s.samples, 1);
s = (await api('PUT', '/api/profile', JSON.stringify({ topics: 'baseball', my_facts: 'I live in Ohio', their_facts: 'Loves jazz', care_contact: 'Nurse Kim', first_message: 'Hey Pat!' }), { 'content-type': 'application/json' })).j;
assert.equal(s.profileIsExample, false);
s = (await api('POST', '/api/clone')).j;
assert.equal(s.voiceId, 'v123');
const add = calls.find((c) => c.url.endsWith('/v1/voices/add'));
assert.ok(add.init.body instanceof FormData);
s = (await api('POST', '/api/agent')).j;
assert.equal(s.agentId, 'agent1'); assert.deepEqual(s.warnings, []);
const created = JSON.parse(calls.find((c) => c.url.endsWith('/agents/create')).init.body);
const prm = created.conversation_config.agent.prompt.prompt;
assert.match(prm, /baseball/); assert.match(prm, /Nurse Kim/); assert.doesNotMatch(prm, /\[\[/);
assert.equal(created.conversation_config.tts.voice_id, 'v123');
assert.equal(created.conversation_config.agent.first_message, 'Hey Pat!');
assert.equal(created.conversation_config.agent.prompt.built_in_tools.transfer_to_number.params.transfers[0].phone_number, '+15551112222');
const prev = await new Promise((resolve) => {
  const r = http.request(base + '/api/preview', { method: 'POST', headers: K }, (res) => resolve(res.headers['content-type']));
  r.end('{"text":"hi"}');
});
assert.equal(prev, 'audio/mpeg');
s = (await api('POST', '/api/mode', JSON.stringify({ mode: 'pass' }))).j;
assert.equal(s.mode, 'pass');
assert.ok(fs.existsSync(path.join(tmp, 'config.json')), 'ids persisted on data disk');
assert.equal(JSON.parse(fs.readFileSync(path.join(tmp, 'config.json'))).ELEVENLABS_AGENT_ID, 'agent1');

server.close();
fs.rmSync(tmp, { recursive: true, force: true });
console.log('All callbuddy tests passed.');
