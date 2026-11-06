#!/usr/bin/env node
// 1) Clones your voice from recordings in ./samples  2) creates the ElevenLabs agent.
// Usage: npm run setup            (both steps, skips any already done)
//        npm run setup -- --voice  (re-clone voice)   --agent (re-create/update agent)
import fs from 'node:fs';
import path from 'node:path';
import { ROOT, loadEnv, setEnvValue, el } from './lib.mjs';

loadEnv();
const args = new Set(process.argv.slice(2));
const need = (k) => { if (!process.env[k]) { console.error(`Missing ${k} in .env`); process.exit(1); } return process.env[k]; };
need('ELEVENLABS_API_KEY');

// ---------- 1. Voice clone ----------
async function cloneVoice() {
  const dir = path.join(ROOT, 'samples');
  const files = fs.readdirSync(dir).filter((f) => /\.(mp3|wav|m4a|ogg|flac|webm|aac)$/i.test(f));
  if (!files.length) {
    console.error('Put 1-3 minutes of clean recordings of YOUR voice in callbuddy/samples/ (mp3/wav/m4a).');
    process.exit(1);
  }
  const form = new FormData();
  form.append('name', `${process.env.MY_NAME || 'My'} (callbuddy)`);
  form.append('description', 'Voice clone of the account owner, made with their consent, for answering family calls.');
  form.append('remove_background_noise', 'true');
  for (const f of files) {
    const buf = fs.readFileSync(path.join(dir, f));
    form.append('files', new Blob([buf]), f);
  }
  console.log(`Cloning voice from ${files.length} file(s)...`);
  const { voice_id } = JSON.parse(await el('/v1/voices/add', { method: 'POST', body: form }));
  setEnvValue('ELEVENLABS_VOICE_ID', voice_id);
  console.log('Voice cloned:', voice_id);
}

// ---------- 2. Agent ----------
function buildPrompt() {
  const profilePath = fs.existsSync(path.join(ROOT, 'profile.json')) ? 'profile.json' : 'profile.example.json';
  if (profilePath === 'profile.example.json') console.warn('No profile.json yet; using profile.example.json. Copy and edit it!');
  const p = JSON.parse(fs.readFileSync(path.join(ROOT, profilePath), 'utf8'));
  const fill = {
    MY_NAME: process.env.MY_NAME, CALLER_NAME: process.env.CALLER_NAME,
    CALLER_CALLS_ME: process.env.CALLER_CALLS_ME || process.env.MY_NAME,
    TOPICS: p.topics, MY_FACTS: p.my_facts, THEIR_FACTS: p.their_facts, CARE_CONTACT: p.care_contact || 'the nurse',
  };
  const prompt = fs.readFileSync(path.join(ROOT, 'prompt.md'), 'utf8').replace(/\[\[(\w+)\]\]/g, (_, k) => fill[k] ?? '');
  return { prompt, firstMessage: p.first_message || "Hi! So good to hear from you. How are you doing?" };
}

function agentBody({ withTools = true, withLlm = true, withTurn = true } = {}) {
  const { prompt, firstMessage } = buildPrompt();
  const promptCfg = { prompt, temperature: 0.6 };
  if (withLlm && process.env.AGENT_LLM) promptCfg.llm = process.env.AGENT_LLM;
  if (withTools) {
    promptCfg.built_in_tools = {
      end_call: {
        name: 'end_call', description: 'End the call after a warm goodbye.',
        params: { system_tool_type: 'end_call' },
      },
      transfer_to_number: {
        name: 'transfer_to_number',
        description: `Transfer the call to ${process.env.MY_NAME}'s real phone in any emergency or safety concern.`,
        params: {
          system_tool_type: 'transfer_to_number',
          transfers: [{
            phone_number: need('MY_CELL'),
            condition: 'The caller is hurt, fell, mentions chest pain, trouble breathing, fire, smoke, being lost, a stranger, someone asking for money, or sounds in danger or very distressed.',
          }],
        },
      },
    };
  }
  const cfg = {
    agent: {
      first_message: firstMessage,
      language: 'en',
      prompt: promptCfg,
      dynamic_variables: { dynamic_variable_placeholders: { time_of_day: 'afternoon', earlier_today: 'none' } },
    },
    tts: {
      voice_id: need('ELEVENLABS_VOICE_ID'),
      model_id: process.env.TTS_MODEL || 'eleven_flash_v2',
      stability: 0.6, similarity_boost: 0.85, speed: 0.92, // a touch slower, easier to follow
    },
  };
  if (withTurn) cfg.turn = { turn_timeout: 12 }; // elderly callers pause longer; don't interrupt
  return { name: `Call buddy for ${process.env.CALLER_NAME || 'family'}`, conversation_config: cfg };
}

async function createOrUpdateAgent() {
  const existing = process.env.ELEVENLABS_AGENT_ID;
  // Try the full config first; fall back if the API rejects a field so setup still finishes.
  const attempts = [
    {}, { withTurn: false }, { withTurn: false, withLlm: false },
    { withTurn: false, withLlm: false, withTools: false },
  ];
  let lastErr;
  for (const opt of attempts) {
    try {
      const body = JSON.stringify(agentBody(opt));
      const init = { method: existing ? 'PATCH' : 'POST', headers: { 'Content-Type': 'application/json' }, body };
      const res = JSON.parse(await el(existing ? `/v1/convai/agents/${existing}` : '/v1/convai/agents/create', init));
      const id = res.agent_id || existing;
      setEnvValue('ELEVENLABS_AGENT_ID', id);
      console.log(`${existing ? 'Updated' : 'Created'} agent: ${id}`);
      if (opt.withTools === false) console.warn('!! Emergency transfer tool was NOT added. Add "Transfer to number" -> MY_CELL in the agent\'s Tools tab in ElevenLabs.');
      if (opt.withLlm === false && process.env.AGENT_LLM) console.warn(`!! LLM "${process.env.AGENT_LLM}" rejected; pick the model in the ElevenLabs dashboard.`);
      return;
    } catch (e) { lastErr = e; console.warn('Retrying with a simpler config:', e.message.slice(0, 300)); }
  }
  throw lastErr;
}

if (args.has('--voice') || !process.env.ELEVENLABS_VOICE_ID) await cloneVoice();
if (args.has('--agent') || !process.env.ELEVENLABS_AGENT_ID || args.has('--voice')) await createOrUpdateAgent();
else console.log('Agent exists. Run `npm run setup -- --agent` after editing prompt.md / profile.json to push changes.');

console.log(`
Next:
  1. Twilio console > Phone Numbers > ${process.env.TWILIO_NUMBER || 'your number'} > Voice:
     "A call comes in" = Webhook  ${process.env.PUBLIC_URL || 'https://YOUR-SERVER'}/voice  (HTTP POST)
  2. ElevenLabs > Agents > Settings > Post-call webhook = ${process.env.PUBLIC_URL || 'https://YOUR-SERVER'}/elevenlabs/post-call
     (copy its secret into ELEVENLABS_WEBHOOK_SECRET)
  3. npm start`);
