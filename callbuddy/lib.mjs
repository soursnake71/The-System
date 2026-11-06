// Shared helpers: .env loading, Twilio, ElevenLabs, call log.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

export const ROOT = path.dirname(fileURLToPath(import.meta.url));
export const DATA = process.env.CALLBUDDY_DATA || path.join(ROOT, 'data');
const EL = 'https://api.elevenlabs.io';

// Values saved by the app (voice id, agent id). Kept on the data disk so they survive restarts.
const configFile = () => path.join(DATA, 'config.json');

export function loadEnv(file = path.join(ROOT, '.env')) {
  if (fs.existsSync(file)) {
    for (const line of fs.readFileSync(file, 'utf8').split(/\r?\n/)) {
      const m = line.match(/^\s*([A-Z0-9_]+)\s*=\s*(.*)\s*$/);
      if (m && process.env[m[1]] === undefined) process.env[m[1]] = m[2].replace(/^["']|["']$/g, '');
    }
  }
  try { Object.assign(process.env, JSON.parse(fs.readFileSync(configFile(), 'utf8'))); } catch {}
}

export function setEnvValue(key, value) {
  let cfg = {};
  try { cfg = JSON.parse(fs.readFileSync(configFile(), 'utf8')); } catch {}
  cfg[key] = value;
  fs.mkdirSync(DATA, { recursive: true });
  fs.writeFileSync(configFile(), JSON.stringify(cfg, null, 1));
  process.env[key] = value;
}

export const digits = (n) => String(n || '').replace(/\D/g, '').replace(/^1(?=\d{10}$)/, '');
export const callerNumbers = () => (process.env.CALLER_NUMBERS || '').split(',').map(digits).filter(Boolean);
export const isCaller = (from) => callerNumbers().includes(digits(from));

export const xml = (s) => String(s).replace(/[<>&"']/g, (c) => ({ '<': '&lt;', '>': '&gt;', '&': '&amp;', '"': '&quot;', "'": '&apos;' }[c]));

// ---------- Twilio ----------
export function twilioSignatureValid(url, params, signature, token = process.env.TWILIO_AUTH_TOKEN) {
  if (!token || !signature) return false;
  const data = url + Object.keys(params).sort().map((k) => k + params[k]).join('');
  const expected = crypto.createHmac('sha1', token).update(data).digest('base64');
  const a = Buffer.from(expected), b = Buffer.from(signature);
  return a.length === b.length && crypto.timingSafeEqual(a, b);
}

export async function sendSms(to, body) {
  const sid = process.env.TWILIO_ACCOUNT_SID, token = process.env.TWILIO_AUTH_TOKEN;
  if (!sid || !token || !to) return;
  const res = await fetch(`https://api.twilio.com/2010-04-01/Accounts/${sid}/Messages.json`, {
    method: 'POST',
    headers: { Authorization: 'Basic ' + Buffer.from(`${sid}:${token}`).toString('base64') },
    body: new URLSearchParams({ To: to, From: process.env.TWILIO_NUMBER, Body: body.slice(0, 1500) }),
  });
  if (!res.ok) console.error('SMS failed', res.status, await res.text());
}

// ---------- ElevenLabs ----------
export async function el(pathname, init = {}) {
  const res = await fetch(EL + pathname, {
    ...init,
    headers: { 'xi-api-key': process.env.ELEVENLABS_API_KEY, ...(init.headers || {}) },
  });
  const text = await res.text();
  if (!res.ok) throw new Error(`ElevenLabs ${pathname} ${res.status}: ${text}`);
  return text;
}

// Ask ElevenLabs for the TwiML that connects this Twilio call to the agent.
export async function registerAgentCall({ from, to, dynamicVariables }) {
  const text = await el('/v1/convai/twilio/register-call', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      agent_id: process.env.ELEVENLABS_AGENT_ID,
      from_number: from,
      to_number: to,
      direction: 'inbound',
      conversation_initiation_client_data: { dynamic_variables: dynamicVariables },
    }),
  });
  if (text.trim().startsWith('<')) return text;
  const j = JSON.parse(text);
  return j.twiml || j.TwiML || text;
}

export function elevenWebhookValid(rawBody, header, secret = process.env.ELEVENLABS_WEBHOOK_SECRET) {
  if (!secret) return true; // not configured: accept (set the secret in production)
  if (!header) return false;
  const parts = Object.fromEntries(header.split(',').map((p) => p.split('=')));
  const t = Number(parts.t);
  if (!t || Math.abs(Date.now() / 1000 - t) > 30 * 60) return false;
  const expected = 'v0=' + crypto.createHmac('sha256', secret).update(`${parts.t}.${rawBody}`).digest('hex');
  const a = Buffer.from(expected), b = Buffer.from(`v0=${parts.v0 || ''}`);
  return a.length === b.length && crypto.timingSafeEqual(a, b);
}

// ---------- Call log ----------
const logFile = () => path.join(DATA, 'calls.json');
export function readCalls() {
  try { return JSON.parse(fs.readFileSync(logFile(), 'utf8')); } catch { return []; }
}
export function addCall(entry) {
  fs.mkdirSync(DATA, { recursive: true });
  const calls = readCalls().filter((c) => Date.now() - c.at < 30 * 864e5); // keep 30 days
  calls.push(entry);
  fs.writeFileSync(logFile(), JSON.stringify(calls, null, 1));
  return calls;
}

const tz = () => process.env.TZ || 'America/New_York';
export const dayKey = (ms) => new Date(ms).toLocaleDateString('en-CA', { timeZone: tz() });
export const clock = (ms) => new Date(ms).toLocaleTimeString('en-US', { timeZone: tz(), hour: 'numeric', minute: '2-digit' });
export function timeOfDay(ms = Date.now()) {
  const h = Number(new Date(ms).toLocaleString('en-US', { timeZone: tz(), hour: 'numeric', hour12: false })) % 24;
  return h < 5 ? 'night' : h < 12 ? 'morning' : h < 17 ? 'afternoon' : h < 21 ? 'evening' : 'night';
}
export function todaysCalls(now = Date.now()) {
  return readCalls().filter((c) => dayKey(c.at) === dayKey(now));
}
export function earlierToday(now = Date.now()) {
  const calls = todaysCalls(now);
  if (!calls.length) return 'none';
  return calls.map((c) => `${clock(c.at)}: ${c.summary || 'chatted'}`).join(' | ').slice(0, 1500);
}

// Words that should go to the top of your text summary.
const FLAGS = {
  EMERGENCY: /\b(fell|fall(en)?|hurt|bleed|chest|can'?t breathe|fire|smoke|lost|stranger|ambulance|911)\b/i,
  MONEY: /\b(money|bank|card|check|pay|bill|wire|gift card|social security|password|scam)\b/i,
  MEDS: /\b(pill|pills|medicine|medication|dose|doctor|prescription)\b/i,
};
export function flagsFor(callerText) {
  return Object.entries(FLAGS).filter(([, re]) => re.test(callerText)).map(([k]) => k);
}
