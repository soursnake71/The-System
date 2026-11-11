/* Which machine this is: the Omega 110, or a friend's edition. The JayQuan-110 app opens index.html?brand=jayquan
   (its APK flavor sets that); everything else is the same app. faceplate.js and app.js read window.BRAND. */
'use strict';
window.BRAND = (() => {
  let id = 'omega';
  try { id = new URLSearchParams(location.search).get('brand') || 'omega'; } catch (e){}
  const brands = {
    omega:{id:'omega', name:'Omega 110', logo:'omega', word:'OMEGA', model:'110', script:'Omega Rhythm', flash:['OMEGA', '110']},
    // JayQuan's edition: his face logo and name, in the 110's colours
    jayquan:{id:'jayquan', name:'JayQuan-110', logo:'image', image:'brand/jayquan-face.png', imageDark:'brand/jayquan-face-dark.png', word:'JayQuan', model:'110', script:'JayQuan Rhythm', flash:['JAYQUAN', '110']},
  };
  const b = brands[id] || brands.omega;
  document.title = b.name;
  return b;
})();
