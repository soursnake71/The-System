#!/usr/bin/env node
// Twilio answers the call -> this server decides: is it the caller? -> hand the call to the
// voice-cloned ElevenLabs agent (or ring you first). After each call: log it and text you a summary.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import {
  DATA, loadEnv, isCaller, xml, twilioSignatureValid, registerAgentCall, elevenWebhookValid,
  addCall, todaysCalls, earlierToday, timeOfDay, clock, flagsFor, sendSms,
} from './lib.mjs';

loadEnv();
const E = process.env;
const stateFile = path.join(DATA, 'state.json');
let state = { mode: E.MODE || 'ai' };
try { state = { ...state, ...JSON.parse(fs.readFileSync(stateFile, 'utf8')) }; } catch {}
const saveState = () => { fs.mkdirSync(DATA, { recursive: true }); fs.writeFileSync(stateFile, JSON.stringify(state)); };

const twiml = (inner) => `<?xml version="1.0" encoding="UTF-8"?><Response>${inner}</Response>`;
const dialMe = (from, extra = '') =>
  `<Dial callerId="${xml(from || E.TWILIO_NUMBER)}" answerOnBridge="true"${extra}>${xml(E.MY_CELL)}</Dial>`;

async function agentTwiml(params) {
  try {
    return await registerAgentCall({
      from: params.From, to: params.To || E.TWILIO_NUMBER,
      dynamicVariables: { time_of_day: timeOfDay(), earlier_today: earlierToday() },
    });
  } catch (e) {
    console.error('Agent unavailable, ringing you instead:', e.message);
    return twiml(dialMe(params.From));
  }
}

async function handleVoice(params) {
  if (!isCaller(params.From)) return twiml(dialMe(params.From)); // anyone else: straight through to you
  console.log(`${clock(Date.now())} call from ${E.CALLER_NAME} (${params.From}), mode=${state.mode}`);
  if (state.mode === 'pass') return twiml(dialMe(params.From));
  if (state.mode === 'ring') {
    return twiml(dialMe(params.From, ` timeout="${Number(E.RING_SECONDS) || 20}" action="/voice/after-ring" method="POST"`));
  }
  return agentTwiml(params);
}

async function handleAfterRing(params) {
  if (params.DialCallStatus === 'completed') return twiml('<Hangup/>'); // you answered
  return agentTwiml(params);
}

async function handlePostCall(body) {
  const evt = JSON.parse(body);
  if (evt.type && evt.type !== 'post_call_transcription') return;
  const d = evt.data || {};
  if (E.ELEVENLABS_AGENT_ID && d.agent_id && d.agent_id !== E.ELEVENLABS_AGENT_ID) return;
  const transcript = d.transcript || [];
  const callerText = transcript.filter((t) => t.role === 'user').map((t) => t.message || '').join(' ');
  const summary = (d.analysis?.transcript_summary || callerText.slice(0, 200) || 'short call').trim();
  const secs = d.metadata?.call_duration_secs || 0;
  const flags = flagsFor(callerText);
  const transferred = JSON.stringify(transcript).includes('transfer_to_number');
  if (transferred) flags.unshift('TRANSFERRED');
  addCall({ at: Date.now(), id: d.conversation_id, secs, summary: summary.slice(0, 300), flags });
  const n = todaysCalls().length;

  if (E.SMS_SUMMARIES !== 'false' || flags.length) {
    const head = flags.length ? `⚠️ ${flags.join(', ')} - ` : '';
    await sendSms(E.MY_CELL, `${head}${E.CALLER_NAME} call #${n} today (${Math.round(secs / 60)} min): ${summary}`);
  }
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let b = ''; req.on('data', (c) => { b += c; if (b.length > 2e6) req.destroy(); });
    req.on('end', () => resolve(b)); req.on('error', reject);
  });
}

export const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://x');
  const send = (code, body, type = 'text/plain') => { res.writeHead(code, { 'Content-Type': type }); res.end(body); };
  try {
    if (req.method === 'POST' && url.pathname.startsWith('/voice')) {
      const raw = await readBody(req);
      const params = Object.fromEntries(new URLSearchParams(raw));
      const fullUrl = (E.PUBLIC_URL || '').replace(/\/$/, '') + req.url;
      if (E.SKIP_TWILIO_SIGNATURE !== 'true' && !twilioSignatureValid(fullUrl, params, req.headers['x-twilio-signature'])) {
        return send(403, 'bad signature');
      }
      const out = url.pathname === '/voice/after-ring' ? await handleAfterRing(params) : await handleVoice(params);
      return send(200, out, 'text/xml');
    }
    if (req.method === 'POST' && url.pathname === '/elevenlabs/post-call') {
      const raw = await readBody(req);
      if (!elevenWebhookValid(raw, req.headers['elevenlabs-signature'])) return send(401, 'bad signature');
      send(200, 'ok'); // reply fast, then process
      return handlePostCall(raw).catch((e) => console.error('post-call', e));
    }
    if (url.pathname === '/mode' || url.pathname === '/calls') {
      if (!E.ADMIN_KEY || E.ADMIN_KEY === 'change-me' || url.searchParams.get('key') !== E.ADMIN_KEY) return send(403, 'no');
      if (url.pathname === '/mode') {
        const set = url.searchParams.get('set');
        if (set && ['ai', 'ring', 'pass'].includes(set)) { state.mode = set; saveState(); }
        return send(200, `mode: ${state.mode}  (set with &set=ai | ring | pass)`);
      }
      const lines = todaysCalls().map((c) => `${clock(c.at)}  ${Math.round(c.secs / 60)}m  ${c.flags?.join(',') || ''}  ${c.summary}`);
      return send(200, lines.join('\n') || 'No calls yet today.');
    }
    if (url.pathname === '/health') return send(200, 'ok');
    send(404, 'not found');
  } catch (e) {
    console.error(e);
    // Never leave the caller in silence: if anything breaks, ring the real phone.
    if (url.pathname.startsWith('/voice')) return send(200, twiml(dialMe('')), 'text/xml');
    send(500, 'error');
  }
});

if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(new URL(import.meta.url).pathname)) {
  for (const k of ['ELEVENLABS_API_KEY', 'ELEVENLABS_AGENT_ID', 'TWILIO_AUTH_TOKEN', 'MY_CELL', 'CALLER_NUMBERS', 'PUBLIC_URL']) {
    if (!E[k]) console.warn(`Warning: ${k} is not set`);
  }
  const port = Number(E.PORT) || 3000;
  server.listen(port, () => console.log(`callbuddy listening on :${port}, mode=${state.mode}`));
}
