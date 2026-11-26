// Writes the "library" table into five/kit/kit.json: for each factory recording, the waveform id, name and kind that
// kits.js loadFactoryKit() gives it (ids 0..23 = the voice it belongs to, 24..51 = WRC-01 cartridge slots) and the
// part of the file kept after trimSilence (start, length in samples). The plugin (plugins/five) builds its waveform
// library from this table, so a voice's WAVEFORM pick (state.wave[v] = id) means the same in the app and the plugin.
// tools/test.mjs checks the table against loadFactoryKit(). Usage: node five/tools/kit-json.mjs
import fs from 'node:fs';
import * as K from '../kits.js';

const url = new URL('../kit/kit.json', import.meta.url), meta = JSON.parse(fs.readFileSync(url, 'utf8'));
export async function factoryLibrary(){
  const kit = await K.loadFactoryKit();
  return kit.waveforms.map((w) => {
    const a = K.parseAudio(fs.readFileSync(new URL('../kit/' + w.file, import.meta.url))), t = K.trimSilence(a.data, a.sr);
    if (t.data.length !== w.data.length) throw new Error(w.file + ': trim mismatch');
    return {file: w.file, id: w.id, name: w.name, kind: w.kind, start: t.lead, length: w.data.length};
  }).sort((x, y) => x.id - y.id);
}
if (process.argv[1] && import.meta.url.endsWith(process.argv[1].split('/').pop())){
  meta.library = await factoryLibrary();
  fs.writeFileSync(url, JSON.stringify(meta, null, 1).replace(/\n {2,}("(?:file|id|name|kind|start|length)")/g, ' $1').replace(/\n {1,}}/g, ' }') + '\n');
  console.log(`kit.json: ${meta.library.length} library entries`);
}
