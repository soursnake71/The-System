/* Inside the XL-Desk VST3 the page talks to the plugin through JUCE's WebView bridge (window.__JUCE__).
   In a normal browser there is no bridge and XLHOST is null, so the page runs its own audio engine. */
window.XLHOST = (() => {
  const J = window.__JUCE__; if (!J || !J.backend) return null;
  const subs = [];
  J.backend.addEventListener('xl', d => { for (const f of subs) f(d); });
  return {send:m => J.backend.emitEvent('xl', m), on:f => subs.push(f)};
})();
