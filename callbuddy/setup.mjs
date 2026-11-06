#!/usr/bin/env node
// Command-line setup (optional: the iPhone app does the same from its Setup screen).
// npm run setup              clone voice + create agent (skips steps already done)
// npm run setup -- --voice   re-clone voice      --agent  push prompt/profile changes
import { loadEnv } from './lib.mjs';
import { cloneVoice, createOrUpdateAgent } from './agent.mjs';

loadEnv();
const args = new Set(process.argv.slice(2));
if (!process.env.ELEVENLABS_API_KEY) { console.error('Missing ELEVENLABS_API_KEY'); process.exit(1); }

try {
  if (args.has('--voice') || !process.env.ELEVENLABS_VOICE_ID) console.log('Voice cloned:', await cloneVoice());
  if (args.has('--agent') || args.has('--voice') || !process.env.ELEVENLABS_AGENT_ID) {
    const r = await createOrUpdateAgent();
    console.log(`${r.updated ? 'Updated' : 'Created'} agent: ${r.agentId}`);
    r.warnings.forEach((w) => console.warn('!!', w));
  } else console.log('Agent exists. Use --agent to push prompt/profile changes.');
} catch (e) { console.error(e.message); process.exit(1); }
