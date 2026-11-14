// Omega Linn: works offline once opened (cache first, then the network refreshes the cache).
const CACHE = 'omega-linn-v1';
const FILES = ['./', 'index.html', 'style.css', 'engine.js', 'machine.js', 'presets.js', 'faceplate.js', 'app.js', 'io.js', 'worklet.js', 'manifest.webmanifest', 'icon.svg', 'kit.js', 'rom/linn.rom'];
self.addEventListener('install', e => { e.waitUntil(caches.open(CACHE).then(c => c.addAll(FILES)).then(() => self.skipWaiting())); });
self.addEventListener('activate', e => { e.waitUntil(caches.keys().then(ks => Promise.all(ks.filter(k => k !== CACHE).map(k => caches.delete(k)))).then(() => self.clients.claim())); });
self.addEventListener('fetch', e => {
  if (e.request.method !== 'GET') return;
  e.respondWith(caches.match(e.request).then(hit => {
    const net = fetch(e.request).then(r => { if (r.ok) caches.open(CACHE).then(c => c.put(e.request, r.clone())); return r; }).catch(() => hit);
    return hit || net;
  }));
});
