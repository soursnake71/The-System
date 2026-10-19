const CACHE = 'xldesk-v2', FILES = ['./', 'index.html', 'host.js', 'spec.js', 'help.js', 'app.js', 'midi.js', 'dsp/console.js', 'art/afs-logo.jpg', 'manifest.webmanifest', 'icon.svg', 'icon-192.png', 'icon-512.png'];
self.addEventListener('install', e => { e.waitUntil(caches.open(CACHE).then(c => c.addAll(FILES)).then(() => self.skipWaiting())); });
self.addEventListener('activate', e => { e.waitUntil(caches.keys().then(ks => Promise.all(ks.filter(k => k !== CACHE).map(k => caches.delete(k)))).then(() => self.clients.claim())); });
// network first so updates arrive; the cache keeps it working offline
self.addEventListener('fetch', e => { if (e.request.method !== 'GET') return; e.respondWith(fetch(e.request, {cache:'no-cache'}).then(r => { const c = r.clone(); if (r.ok) caches.open(CACHE).then(k => k.put(e.request, c)); return r; }).catch(() => caches.match(e.request, {ignoreSearch:true}))); });
