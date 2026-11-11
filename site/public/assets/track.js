/* =====================================================================
   Counting visits to jamesmireku.com, for the team's dashboard.

   One small message per page: which page, which site sent the visitor
   (or the utm_source of a shared link), and phone, tablet or computer.
   The site's Worker adds the country. A random id kept in sessionStorage
   ties the pages of one visit together and is gone when the tab closes.
   No cookies, nothing stored about who anyone is, no third party.

   A browser that sends Do Not Track or Global Privacy Control is not
   counted at all. Nothing here can break or slow a page: everything runs
   after load, and every failure is ignored.
   ===================================================================== */
(function () {
  window.mirekuTrack = function () {};
  try {
    if (navigator.doNotTrack === '1' || window.doNotTrack === '1' || navigator.globalPrivacyControl) return;
    if (navigator.webdriver) return;

    var sid = '';
    try {
      sid = sessionStorage.getItem('mireku.sid') || '';
      if (!sid) {
        sid = (crypto.randomUUID ? crypto.randomUUID() : String(Math.random()).slice(2) + Date.now());
        sessionStorage.setItem('mireku.sid', sid);
      }
    } catch (e) { sid = String(Math.random()).slice(2) + Date.now(); }

    var send = function (body) {
      body.sid = sid;
      var data = JSON.stringify(body);
      try {
        if (navigator.sendBeacon && navigator.sendBeacon('/api/hit', new Blob([data], { type: 'application/json' }))) return;
      } catch (e) {}
      try { fetch('/api/hit', { method: 'POST', headers: { 'content-type': 'application/json' }, body: data, keepalive: true }).catch(function () {}); } catch (e) {}
    };

    window.mirekuTrack = function (name, detail) {
      try { send({ t: 'event', name: String(name), detail: detail ? String(detail).slice(0, 120) : undefined }); } catch (e) {}
    };

    var source = '';
    try { source = new URLSearchParams(location.search).get('utm_source') || ''; } catch (e) {}
    if (!source) { try { source = document.referrer ? new URL(document.referrer).hostname : ''; } catch (e) {} }

    var w = Math.min(screen.width || 9999, window.innerWidth || 9999);
    var device = /iPad|Tablet/i.test(navigator.userAgent) || (w >= 600 && w < 1024 && 'ontouchstart' in window) ? 'tablet'
               : (w < 600 ? 'phone' : 'computer');

    var path = location.pathname.replace(/\.html$/, '').replace(/\/index$/, '/') || '/';
    var go = function () { send({ t: 'view', path: path, ref: source, device: device }); };
    if (document.readyState === 'complete') go(); else window.addEventListener('load', go);
  } catch (e) {}
})();
