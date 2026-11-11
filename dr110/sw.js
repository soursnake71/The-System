// Omega 110: works offline once opened (cache first, then the network refreshes the cache).
const CACHE = 'omega-110-v7';
const FILES = ['./', 'index.html', 'style.css', 'engine.js', 'seq.js', 'presets.js', 'brand.js', 'brand/jayquan-face.png', 'brand/jayquan-face-blue.png', 'faceplate.js', 'app.js', 'omega.js', 'io.js', 'worklet.js', 'manifest.webmanifest', 'icon.svg',
  'samples/bd.wav', 'samples/sd.wav', 'samples/oh.wav', 'samples/oh-long.wav', 'samples/ch.wav', 'samples/ch-long.wav', 'samples/cy.wav', 'samples/hc.wav'];
self.addEventListener('install', e => { e.waitUntil(caches.open(CACHE).then(c => c.addAll(FILES)).then(() => self.skipWaiting())); });
self.addEventListener('activate', e => { e.waitUntil(caches.keys().then(ks => Promise.all(ks.filter(k => k !== CACHE).map(k => caches.delete(k)))).then(() => self.clients.claim())); });
self.addEventListener('fetch', e => {
  if (e.request.method !== 'GET') return;
  e.respondWith(caches.match(e.request).then(hit => {
    const net = fetch(e.request).then(r => { if (r.ok) caches.open(CACHE).then(c => c.put(e.request, r.clone())); return r; }).catch(() => hit);
    return hit || net;
  }));
});
