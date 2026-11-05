/* =====================================================================
   The account page: sign in, then the MIREKU licence key, the computers
   using it, and the installers.

   Everything here goes through the same Supabase project as
   amanorsac.studio, with the publishable key and the visitor's own
   session, so row-level security decides what this page can see: a
   visitor reads only their own purchase and their own devices.

   The key itself is minted by the database when the purchase row is
   written (the mint_license_key trigger), and the installers are handed
   out by amanorsac.studio's /api/app-download, which checks ownership
   again before signing a download ticket. This page draws; it decides
   nothing.
   ===================================================================== */
(function () {
  var C = window.MIREKU;
  var M = window.Mireku;
  var sb = M.sb();
  var $ = function (id) { return document.getElementById(id); };
  var params = new URLSearchParams(location.search);
  var PLATFORM = { windows: 'Windows', mac: 'Mac', 'mac-arm64': 'Mac (Apple silicon)', 'mac-x64': 'Mac (Intel)' };

  function show(id) {
    ['loading', 'signin', 'reset', 'mine'].forEach(function (x) { $(x).hidden = x !== id; });
  }
  function say(el, text, kind) { el.className = 'msg' + (kind ? ' ' + kind : ''); el.textContent = text || ''; }
  function when(iso) {
    try { return new Date(iso).toLocaleDateString('en-GB', { day: 'numeric', month: 'long', year: 'numeric' }); }
    catch (e) { return ''; }
  }

  if (!sb) { $('loading').textContent = 'The page did not finish loading. Refresh to try again.'; return; }

  /* ---------------- signing in, up, and back in ---------------- */
  var mode = 'in';
  function setMode(m) {
    mode = m;
    $('signin-title').textContent = m === 'up' ? 'Create an account' : m === 'forgot' ? 'Reset your password' : 'Sign in';
    $('go').textContent = m === 'up' ? 'Create account' : m === 'forgot' ? 'Send reset link' : 'Sign in';
    $('row-name').hidden = m !== 'up';
    $('row-pass').hidden = m === 'forgot';
    $('pass').autocomplete = m === 'up' ? 'new-password' : 'current-password';
    $('sw-new').hidden = m !== 'in';
    $('sw-old').hidden = m === 'in';
    $('sw-forgot').hidden = m !== 'in';
    say($('signin-msg'), '');
  }
  document.querySelectorAll('[data-mode]').forEach(function (b) {
    b.addEventListener('click', function () { setMode(b.dataset.mode); });
  });

  var back = location.origin + '/account' + (params.get('buy') ? '?buy=1' : '');

  $('f-signin').addEventListener('submit', function (e) {
    e.preventDefault();
    var msg = $('signin-msg');
    var email = $('email').value.trim();
    var pass = $('pass').value;
    if (!/^[^@\s]+@[^@\s]+\.[^@\s]+$/.test(email)) return say(msg, 'Enter your email address.', 'err');
    if (mode !== 'forgot' && pass.length < 8) return say(msg, 'Passwords are at least 8 characters.', 'err');
    $('go').disabled = true;
    say(msg, 'One moment…');

    var done = function () { $('go').disabled = false; };
    if (mode === 'forgot') {
      sb.auth.resetPasswordForEmail(email, { redirectTo: location.origin + '/account?reset=1' }).then(function (r) {
        done();
        if (r.error) return say(msg, r.error.message, 'err');
        say(msg, 'If that address has an account, a reset link is on its way. Check your inbox.', 'ok');
      });
    } else if (mode === 'up') {
      sb.auth.signUp({ email: email, password: pass,
                       options: { data: { full_name: $('name').value.trim() }, emailRedirectTo: back } }).then(function (r) {
        done();
        if (r.error) return say(msg, r.error.message, 'err');
        if (r.data && r.data.session) return; // signed straight in; the listener below takes over
        say(msg, 'Check your inbox: we have sent a link to confirm your email. Open it on this device to finish.', 'ok');
      });
    } else {
      sb.auth.signInWithPassword({ email: email, password: pass }).then(function (r) {
        done();
        if (!r.error) return;
        var t = /confirm/i.test(r.error.message)
          ? 'Your email is not confirmed yet. Open the link we sent you, then sign in.'
          : /invalid/i.test(r.error.message) ? 'That email and password do not match an account.' : r.error.message;
        say(msg, t, 'err');
      });
    }
  });

  $('f-reset').addEventListener('submit', function (e) {
    e.preventDefault();
    var p = $('newpass').value;
    if (p.length < 8) return say($('reset-msg'), 'Passwords are at least 8 characters.', 'err');
    sb.auth.updateUser({ password: p }).then(function (r) {
      if (r.error) return say($('reset-msg'), r.error.message, 'err');
      history.replaceState(null, '', '/account');
      recovering = false;
      sb.auth.getSession().then(function (s) { route(s.data.session); });
    });
  });

  $('signout').addEventListener('click', function () { sb.auth.signOut(); });

  /* ---------------- what the account holds ---------------- */
  var session = null;

  function load() {
    $('who').textContent = session.user.email || '';
    show('mine');
    return sb.from('purchases')
      .select('id, license_key, max_devices, purchased_at, kind, expires_at')
      .eq('app', C.APP).order('purchased_at', { ascending: true }).limit(1)
      .then(function (r) {
        if (r.error) {
          $('owned').hidden = true; $('notowned').hidden = false;
          $('notowned-text').textContent = 'Your account could not be read just now. Refresh to try again.';
          return null;
        }
        var row = r.data && r.data[0];
        if (!row || !row.license_key) return notOwned();
        return owned(row);
      });
  }

  /* Paystack sends the buyer back before, or just after, its webhook has
     written the purchase. So on the way back from a payment, keep looking
     for a minute instead of telling someone who has just paid that they
     own nothing. */
  var waited = 0;
  function notOwned() {
    $('owned').hidden = true;
    $('notowned').hidden = false;
    var buy = $('buy');
    if (params.get('purchased') && waited < 20) {
      waited++;
      $('notowned').querySelector('h2').textContent = 'Thank you. Your payment is being confirmed';
      $('notowned-text').textContent = 'Your licence key appears here within a minute. This page checks for it by itself.';
      buy.hidden = true;
      setTimeout(load, 3000);
      return;
    }
    if (params.get('purchased')) {
      $('notowned').querySelector('h2').textContent = 'Your payment is still being confirmed';
      $('notowned-text').textContent = 'This can take a few minutes. Refresh this page shortly. If your key has not appeared within the hour, reply to your Paystack receipt and we will sort it out.';
      buy.hidden = true;
      return;
    }
    M.offer().then(function (o) {
      if (!o.onSale) return;
      buy.disabled = false;
      buy.textContent = 'Buy MIREKU · ' + o.price.display;
      $('notowned-text').textContent = 'One payment of ' + o.price.display + '. ' + (o.price.note || '') + ' Your licence key and downloads appear here straight after.';
      if (params.get('buy')) { history.replaceState(null, '', '/account'); params.delete('buy'); startBuy(); }
    });
  }

  function startBuy() {
    var buy = $('buy');
    buy.disabled = true;
    say($('buy-msg'), 'Opening the checkout…');
    M.checkout().catch(function (e) { buy.disabled = false; say($('buy-msg'), e.message, 'err'); });
  }
  $('buy').addEventListener('click', startBuy);

  function owned(row) {
    if (params.get('purchased')) history.replaceState(null, '', '/account');
    $('notowned').hidden = true;
    $('owned').hidden = false;
    $('key').textContent = row.license_key;
    $('copy').dataset.key = row.license_key;

    var t = $('trial');
    if (row.expires_at) {
      var days = Math.ceil((new Date(row.expires_at) - Date.now()) / 86400000);
      t.hidden = false;
      t.className = 'msg ' + (days > 0 ? 'ok' : 'err');
      t.textContent = (row.kind === 'trial' ? 'Trial' : 'Licence') + (days > 0
        ? ': ' + days + (days === 1 ? ' day left' : ' days left') + ', until ' + when(row.expires_at) + '.'
        : ' ended on ' + when(row.expires_at) + '.');
    } else { t.hidden = true; }

    devices(row);
    downloads();
  }

  function devices(row) {
    return sb.from('device_activations')
      .select('id, device_name, device_id, last_seen')
      .eq('license_id', row.id).is('revoked_at', null).order('last_seen', { ascending: false })
      .then(function (r) {
        var list = (r.data || []);
        $('seats').textContent = list.length + ' of ' + row.max_devices + ' in use. ' +
          (list.length ? 'Remove a computer you no longer use to free its place.' : 'No computer has activated this key yet.');
        $('devices').innerHTML = '';
        list.forEach(function (d) {
          var li = document.createElement('li');
          var name = document.createElement('span');
          name.innerHTML = '<b></b><br><small></small>';
          name.querySelector('b').textContent = d.device_name || d.device_id;
          name.querySelector('small').textContent = 'Last seen ' + when(d.last_seen);
          var b = document.createElement('button');
          b.className = 'btn ghost small'; b.type = 'button'; b.textContent = 'Remove';
          b.addEventListener('click', function () {
            b.disabled = true; b.textContent = 'Removing…';
            sb.rpc('revoke_device', { p_device_activation_id: d.id }).then(function (x) {
              if (x.error) { b.disabled = false; b.textContent = 'Remove'; return; }
              devices(row);
            });
          });
          li.appendChild(name); li.appendChild(b);
          $('devices').appendChild(li);
        });
      });
  }

  function downloads() {
    var box = $('downloads');
    M.offer().then(function (o) {
      box.innerHTML = '';
      var plats = o.platforms.filter(function (p) { return PLATFORM[p]; });
      if (!plats.length) { say($('dl-msg'), 'The installers are being prepared. They appear here as soon as they are ready.'); return; }
      say($('dl-msg'), o.version ? 'Version ' + o.version : '');
      plats.forEach(function (p) {
        var b = document.createElement('button');
        b.className = 'btn'; b.type = 'button'; b.textContent = PLATFORM[p];
        b.addEventListener('click', function () { download(p, b); });
        box.appendChild(b);
      });
    });
  }

  /* amanorsac.studio checks ownership and signs a ticket that lasts
     fifteen minutes; the browser then fetches the installer from there. */
  function download(platform, b) {
    b.disabled = true;
    say($('dl-msg'), 'Preparing your download…');
    fetch(C.STUDIO + '/api/app-download', {
      method: 'POST',
      headers: { 'content-type': 'application/json', Authorization: 'Bearer ' + session.access_token },
      body: JSON.stringify({ app: C.APP, platform: platform })
    }).then(function (r) { return r.json().catch(function () { return {}; }); })
      .then(function (out) {
        b.disabled = false;
        if (!out.url) return say($('dl-msg'), out.message || 'The download could not be started. Try again shortly.', 'err');
        say($('dl-msg'), 'Your download is starting.', 'ok');
        location.href = /^https:/.test(out.url) ? out.url : C.STUDIO + out.url;
      })
      .catch(function () { b.disabled = false; say($('dl-msg'), 'The download could not be started. Check your connection.', 'err'); });
  }

  $('copy').addEventListener('click', function () {
    var b = $('copy');
    var k = b.dataset.key || '';
    (navigator.clipboard ? navigator.clipboard.writeText(k) : Promise.reject())
      .then(function () { b.textContent = 'Copied'; }, function () { b.textContent = 'Select and copy'; })
      .then(function () { setTimeout(function () { b.textContent = 'Copy'; }, 1600); });
  });

  /* ---------------- routing ---------------- */
  var recovering = params.get('reset') === '1';
  function route(s) {
    session = s;
    if (recovering) return show('reset');
    if (!s) { setMode(mode); return show('signin'); }
    load();
  }

  sb.auth.onAuthStateChange(function (event, s) {
    if (event === 'PASSWORD_RECOVERY') { recovering = true; session = s; return show('reset'); }
    if (event === 'SIGNED_IN' || event === 'SIGNED_OUT' || event === 'INITIAL_SESSION') {
      // let the client finish its own work before reading tables
      setTimeout(function () { route(s); }, 0);
    }
  });
})();
