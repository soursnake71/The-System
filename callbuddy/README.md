# Call Buddy

An AI that speaks in **your cloned voice** and answers calls from one person: a family member with Alzheimer's who calls several times a day and doesn't remember the earlier calls. It is patient every time and never says "you already called." It transfers emergencies to your real phone and texts you a short summary after each call.

```
Their phone ──► Twilio number ──► server.mjs ──┬─ caller's number? ─► ElevenLabs agent (your voice)
                                               ├─ mode=ring ─► your cell first, agent if no answer
                                               └─ anyone else ─► your cell
ElevenLabs ── post-call webhook ──► server.mjs ──► text summary to you (⚠️ money / meds / emergency flags)
```

## What you need
- An **ElevenLabs** account on a plan that allows voice cloning. Instant Voice Cloning is in the Starter plan and up.
- A **Twilio** account and one voice- and SMS-capable phone number, about $1.15 a month plus per-minute charges.
- Somewhere to run Node 18+ with a public HTTPS URL: Render, Railway, Fly.io, a home machine with ngrok, and so on.

## Setup
1. **Record your voice.** Put 1–3 minutes of clean recordings of yourself in `samples/` (see `samples/README.md`).
2. **Configure.** `cp .env.example .env` and `cp profile.example.json profile.json`, then fill both in. The profile is what makes the calls sound like you: topics they enjoy, safe facts about your life, the names of their nurse and family. Everything in `prompt.md` can be edited.
3. **Clone your voice and create the agent.** `npm run setup` writes `ELEVENLABS_VOICE_ID` and `ELEVENLABS_AGENT_ID` into `.env`. After editing `prompt.md` or `profile.json`, run `npm run setup -- --agent` to push the changes.
4. **Deploy** this folder and run `npm start`. Set `PUBLIC_URL` to its address.
5. **Twilio:** under Phone Numbers, open your number and set "A call comes in" to Webhook `PUBLIC_URL/voice` (POST).
6. **ElevenLabs:** under Agents → Settings, set the post-call webhook to `PUBLIC_URL/elevenlabs/post-call` and put its secret in `ELEVENLABS_WEBHOOK_SECRET`.
7. In the ElevenLabs dashboard, open the agent and **test it** in the browser before going live. Check that the Tools tab shows **Transfer to number** pointing to your cell. If setup printed a warning that it could not add the tool, add it there by hand.

## Getting their calls to the Twilio number
Your phone can't hand a live call's audio to an app, so their calls have to reach the Twilio number. Pick one:

- **A. Change their contact entry (recommended, most reliable).** Ask the caregiver or facility to change the number saved under your name, or their speed dial, to the Twilio number. They keep pressing the same button. Calls from anyone else to the Twilio number are passed straight through to your cell.
- **B. Carrier "forward when busy".** On most US carriers, dial `*67*<twilio number>#` (GSM) or ask your carrier to turn on forward-when-busy or no-answer. When their call comes in, tap Decline: the carrier forwards it to Twilio, and the server sees their number and answers. Other callers you decline also land on Twilio and get passed back to your cell, so you may want `MODE=pass` as the default for unknown numbers. That is already how it behaves.

## Day to day (from your phone's browser)
- `PUBLIC_URL/mode?key=ADMIN_KEY&set=ring`: ring me first, and the agent answers if I don't pick up within `RING_SECONDS`. Keep this shorter than your voicemail pickup time.
- `...&set=ai`: the agent always answers. `...&set=pass`: the agent is off.
- `PUBLIC_URL/calls?key=ADMIN_KEY`: today's calls with summaries.

Each summary text looks like: `⚠️ MONEY - Pat call #3 today (4 min): Talked about the garden; asked whether a bill needs paying.`

## Safety built in
- **Emergencies** (a fall, pain, fire, being lost, a stranger, someone asking for money) → the agent transfers the call to your cell immediately. If the transfer fails, it tells them to dial 911.
- It never agrees to money, purchases or account details, never gives medication advice, and never promises a visit.
- If ElevenLabs is down or anything errors, the call **rings your phone** instead of going silent.
- Twilio and ElevenLabs webhook signatures are verified. `/mode` and `/calls` need `ADMIN_KEY`.
- If they sincerely and repeatedly ask whether it's a machine, the agent won't flatly deny it. It says it's your phone helper and that you'll call back. You can change this in `prompt.md`, but there are good reasons to keep it.

## Please also
- **Tell their caregiver or facility** that this is set up. A person who knows can then check on anything the summaries flag.
- **Recording and consent laws** differ by state and country. ElevenLabs stores transcripts. Look up your local rules, and turn on zero-retention in ElevenLabs if you want.
- **Clone only your own voice.** ElevenLabs requires consent from the voice's owner.
- Still call them yourself sometimes. The summaries make it easy to bring up what they talked about.

## Test
`npm test` runs an offline test with fake Twilio and ElevenLabs. It covers routing, signatures, the ring-first fallback, memory across calls, and the summary texts.
