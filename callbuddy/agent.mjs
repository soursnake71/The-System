// Voice cloning + agent creation, used by setup.mjs (command line) and the iPhone app (server.mjs).
import fs from 'node:fs';
import path from 'node:path';
import { ROOT, DATA, setEnvValue, el } from './lib.mjs';

const AUDIO = /\.(mp3|wav|m4a|mp4|ogg|flac|webm|aac)$/i;
const sampleDirs = () => [path.join(ROOT, 'samples'), path.join(DATA, 'samples')];

export function listSamples() {
  return sampleDirs().flatMap((d) => (fs.existsSync(d) ? fs.readdirSync(d) : [])
    .filter((f) => AUDIO.test(f)).map((f) => path.join(d, f)));
}

export function saveSample(buf, contentType = '') {
  const ext = /mp4|m4a|aac/.test(contentType) ? 'm4a' : /webm/.test(contentType) ? 'webm'
    : /wav/.test(contentType) ? 'wav' : /mpeg|mp3/.test(contentType) ? 'mp3' : 'm4a';
  const dir = path.join(DATA, 'samples');
  fs.mkdirSync(dir, { recursive: true });
  const file = path.join(dir, `rec-${Date.now()}.${ext}`);
  fs.writeFileSync(file, buf);
  return file;
}

export function clearSamples() {
  const dir = path.join(DATA, 'samples');
  if (fs.existsSync(dir)) fs.rmSync(dir, { recursive: true, force: true });
}

export function readProfile() {
  for (const f of [path.join(DATA, 'profile.json'), path.join(ROOT, 'profile.json'), path.join(ROOT, 'profile.example.json')]) {
    if (fs.existsSync(f)) return { ...JSON.parse(fs.readFileSync(f, 'utf8')), _example: f.endsWith('example.json') };
  }
  return {};
}

export function saveProfile(p) {
  const keep = ['topics', 'my_facts', 'their_facts', 'care_contact', 'first_message'];
  const clean = Object.fromEntries(keep.map((k) => [k, String(p[k] ?? '').slice(0, 2000)]));
  fs.mkdirSync(DATA, { recursive: true });
  fs.writeFileSync(path.join(DATA, 'profile.json'), JSON.stringify(clean, null, 1));
}

export async function cloneVoice() {
  const files = listSamples();
  if (!files.length) throw new Error('Record (or add) at least one sample of your voice first.');
  const form = new FormData();
  form.append('name', `${process.env.MY_NAME || 'My'} (callbuddy)`);
  form.append('description', 'Voice clone of the account owner, made with their consent, for answering family calls.');
  form.append('remove_background_noise', 'true');
  for (const f of files) form.append('files', new Blob([fs.readFileSync(f)]), path.basename(f));
  const old = process.env.ELEVENLABS_VOICE_ID;
  const { voice_id } = JSON.parse(await el('/v1/voices/add', { method: 'POST', body: form }));
  setEnvValue('ELEVENLABS_VOICE_ID', voice_id);
  if (old && old !== voice_id) el(`/v1/voices/${old}`, { method: 'DELETE' }).catch(() => {});
  return voice_id;
}

export async function previewVoice(text) {
  const id = process.env.ELEVENLABS_VOICE_ID;
  if (!id) throw new Error('Clone your voice first.');
  const res = await fetch(`https://api.elevenlabs.io/v1/text-to-speech/${id}?output_format=mp3_44100_64`, {
    method: 'POST',
    headers: { 'xi-api-key': process.env.ELEVENLABS_API_KEY, 'Content-Type': 'application/json' },
    body: JSON.stringify({ text: String(text).slice(0, 300), model_id: process.env.TTS_MODEL || 'eleven_flash_v2' }),
  });
  if (!res.ok) throw new Error(`Preview failed ${res.status}: ${await res.text()}`);
  return Buffer.from(await res.arrayBuffer());
}

function buildPrompt() {
  let p = readProfile();
  // Never feed the example's made-up facts to the AI.
  if (p._example) p = { topics: 'everyday things: the weather, meals, music, family, the garden', my_facts: '', their_facts: '', care_contact: '' };
  const fill = {
    MY_NAME: process.env.MY_NAME, CALLER_NAME: process.env.CALLER_NAME,
    CALLER_CALLS_ME: process.env.CALLER_CALLS_ME || process.env.MY_NAME,
    TOPICS: p.topics || 'everyday things: the weather, meals, music', MY_FACTS: p.my_facts || '(none given; keep it vague and positive)', THEIR_FACTS: p.their_facts || '(none given; ask simple, friendly questions)', CARE_CONTACT: p.care_contact || 'the nurse',
  };
  const prompt = fs.readFileSync(path.join(ROOT, 'prompt.md'), 'utf8').replace(/\[\[(\w+)\]\]/g, (_, k) => fill[k] ?? '');
  return { prompt, firstMessage: p.first_message || 'Hi! So good to hear from you. How are you doing?' };
}

function agentBody({ withTools = true, withLlm = true, withTurn = true } = {}) {
  const { prompt, firstMessage } = buildPrompt();
  const E = process.env;
  if (!E.ELEVENLABS_VOICE_ID) throw new Error('Clone your voice first.');
  if (!E.MY_CELL) throw new Error('MY_CELL is not set.');
  const promptCfg = { prompt, temperature: 0.6 };
  if (withLlm && E.AGENT_LLM) promptCfg.llm = E.AGENT_LLM;
  if (withTools) {
    promptCfg.built_in_tools = {
      end_call: { name: 'end_call', description: 'End the call after a warm goodbye.', params: { system_tool_type: 'end_call' } },
      transfer_to_number: {
        name: 'transfer_to_number',
        description: `Transfer the call to ${E.MY_NAME}'s real phone in any emergency or safety concern.`,
        params: {
          system_tool_type: 'transfer_to_number',
          transfers: [{
            phone_number: E.MY_CELL,
            condition: 'The caller is hurt, fell, mentions chest pain, trouble breathing, fire, smoke, being lost, a stranger, someone asking for money, or sounds in danger or very distressed.',
          }],
        },
      },
    };
  }
  const cfg = {
    agent: {
      first_message: firstMessage, language: 'en', prompt: promptCfg,
      dynamic_variables: { dynamic_variable_placeholders: { time_of_day: 'afternoon', earlier_today: 'none' } },
    },
    tts: {
      voice_id: E.ELEVENLABS_VOICE_ID, model_id: E.TTS_MODEL || 'eleven_flash_v2',
      stability: 0.6, similarity_boost: 0.85, speed: 0.92, // a touch slower, easier to follow
    },
  };
  if (withTurn) cfg.turn = { turn_timeout: 12 }; // elderly callers pause longer; don't interrupt
  return { name: `Call buddy for ${E.CALLER_NAME || 'family'}`, conversation_config: cfg };
}

// Returns { agentId, warnings[] }. Falls back to simpler configs if the API rejects a field.
export async function createOrUpdateAgent() {
  const existing = process.env.ELEVENLABS_AGENT_ID;
  const attempts = [{}, { withTurn: false }, { withTurn: false, withLlm: false },
    { withTurn: false, withLlm: false, withTools: false }];
  let lastErr;
  for (const opt of attempts) {
    const body = JSON.stringify(agentBody(opt)); // missing voice/cell throws here, no retry
    try {
      const init = { method: existing ? 'PATCH' : 'POST', headers: { 'Content-Type': 'application/json' }, body };
      const res = JSON.parse(await el(existing ? `/v1/convai/agents/${existing}` : '/v1/convai/agents/create', init));
      const agentId = res.agent_id || existing;
      setEnvValue('ELEVENLABS_AGENT_ID', agentId);
      const warnings = [];
      if (opt.withTools === false) warnings.push('The emergency transfer tool was NOT added. In ElevenLabs, open the agent > Tools > add "Transfer to number" with your cell.');
      if (opt.withLlm === false && process.env.AGENT_LLM) warnings.push(`The model "${process.env.AGENT_LLM}" was rejected; pick one in the ElevenLabs agent settings.`);
      return { agentId, updated: !!existing, warnings };
    } catch (e) { lastErr = e; console.warn('Agent config rejected, retrying simpler:', e.message.slice(0, 300)); }
  }
  throw lastErr;
}
