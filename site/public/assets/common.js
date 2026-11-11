/* =====================================================================
   Shared by both pages: one Supabase client, what the studio's catalog
   says about MIREKU, and the road to Paystack.

   Nothing here decides a price. The page asks amanorsac.studio what
   MIREKU costs only so a button can carry a figure; the create-app-
   paystack function reads catalog.json again on the server before it
   charges, and ignores anything a page says about money.
   ===================================================================== */
(function () {
  var C = window.MIREKU;
  var client = null;

  function sb() {
    if (client) return client;
    if (!window.supabase || !window.supabase.createClient) return null;
    client = window.supabase.createClient(C.SUPABASE_URL, C.SUPABASE_KEY);
    return client;
  }

  /* What the studio says about MIREKU right now: whether it is on sale,
     its platforms, and the local price for Paystack. Both endpoints are
     public and answer any origin. A failure of either reads as "not on
     sale", never as an error a visitor has to understand. */
  /* jamesmireku.com/?preview=sale shows the page as it will look on sale,
     with the planned price, so the team can check it before launch. Nothing
     can be bought through it: the buy button only says it is a preview. */
  var PREVIEW = /[?&]preview=sale\b/.test(location.search);
  var PREVIEW_PRICE = { display: 'GHS 1,417', usd: '$119', note: 'Mobile money, bank transfer or a local card.' };

  var offerPromise = null;
  function offer() {
    if (offerPromise) return offerPromise;
    if (PREVIEW) {
      offerPromise = Promise.resolve({ app: null, onSale: true, platforms: [], version: null, price: PREVIEW_PRICE, preview: true });
      var flag = document.createElement('p');
      flag.textContent = 'Preview of the on-sale page. Nothing can be bought here.';
      flag.style.cssText = 'margin:0;padding:8px 16px;text-align:center;font:600 13px Inter,sans-serif;background:#d9a441;color:#1a1206';
      document.body.insertBefore(flag, document.body.firstChild);
      return offerPromise;
    }
    var cat = fetch(C.STUDIO + '/api/catalog', { cache: 'no-store' })
      .then(function (r) { return r.ok ? r.json() : null; })
      .then(function (c) { return (c && c.apps && c.apps[C.APP]) || null; })
      .catch(function () { return null; });
    var pay = fetch(C.STUDIO + '/api/pay-options?app=' + encodeURIComponent(C.APP), { cache: 'no-store' })
      .then(function (r) { return r.ok ? r.json() : null; })
      .catch(function () { return null; });
    offerPromise = Promise.all([cat, pay]).then(function (x) {
      var app = x[0], p = x[1] && x[1].paystack;
      var onSale = !!(C.SELLING && app && app.status === 'available' && !app.free && p && p.amount > 0);
      return {
        app: app,
        onSale: onSale,
        platforms: (app && app.platforms) || [],
        version: (app && app.version) || null,
        price: onSale ? { display: p.display, usd: p.usd_display, note: p.note } : null
      };
    });
    return offerPromise;
  }

  /* Send a signed-in buyer to Paystack. Paystack returns them to
     <this site>/mireku?purchased=1, which forwards to the account page,
     where the key appears once Paystack's webhook has recorded the sale. */
  /* A discount code typed beside the buy button. Tidied the way the server
     tidies it; kept for this tab so it survives the trip through sign-in.
     The page only carries it: create-app-paystack looks the code up and
     works out the price itself, and says so if the code is no good. */
  function cleanCode(v) { return String(v || '').toUpperCase().replace(/[^A-Z0-9-]/g, '').slice(0, 40); }
  function rememberCode(v) { try { v ? sessionStorage.setItem('mireku.code', v) : sessionStorage.removeItem('mireku.code'); } catch (e) {} }
  function rememberedCode() { try { return cleanCode(sessionStorage.getItem('mireku.code')); } catch (e) { return ''; } }

  function checkout(code) {
    code = cleanCode(code);
    if (PREVIEW) return Promise.reject(new Error('This is a preview, so nothing was charged.' + (code ? ' The code ' + code + ' would be checked at this point.' : '')));
    rememberCode(code);
    var s = sb();
    if (!s) return Promise.reject(new Error('The page did not finish loading. Refresh and try again.'));
    return s.auth.getSession().then(function (r) {
      var session = r.data && r.data.session;
      if (!session) { location.href = '/account?buy=1'; return { redirected: true }; }
      return fetch(C.SUPABASE_URL + '/functions/v1/create-app-paystack', {
        method: 'POST',
        headers: {
          'content-type': 'application/json',
          apikey: C.SUPABASE_KEY,
          Authorization: 'Bearer ' + session.access_token
        },
        body: JSON.stringify(code ? { app: C.APP, code: code } : { app: C.APP })
      }).then(function (res) {
        return res.json().catch(function () { return {}; }).then(function (out) {
          if (out.already_owned) { location.href = '/account'; return { redirected: true }; }
          if (!res.ok || !out.url) throw new Error(out.error || 'The checkout could not be opened. Try again shortly.');
          location.href = out.url;
          return { redirected: true };
        });
      });
    });
  }

  window.Mireku = { sb: sb, offer: offer, checkout: checkout, cleanCode: cleanCode, rememberedCode: rememberedCode };
})();
