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
                       options: { data: { full_name: $('name').value.trim(), signup_site: 'jamesmireku.com' }, emailRedirectTo: back } }).then(function (r) {
        done();
        if (r.error) return say(msg, r.error.message, 'err');
        if (window.mirekuTrack) window.mirekuTrack('account_created');
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
  var isTeam = false;

  function load() {
    $('who').textContent = session.user.email || '';
    show('mine');
    // The MIREKU team gets a door to the dashboard. The database decides who
    // that is (is_mireku_admin); for everyone else this button never appears.
    sb.rpc('is_mireku_admin').then(function (r) {
      var team = !r.error && r.data === true;
      isTeam = team;
      $('team').hidden = !team;
      if (team) $('notowned').hidden = true;   // the team tests without buying
      if (team) downloads();   // the team may download to test, without owning it
      if (team && params.get('next') === '/admin') location.replace('/admin');
    });
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
    $('notowned').hidden = isTeam;
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
      $('code-row').hidden = false;
      if (!$('code').value) $('code').value = M.rememberedCode();
      $('notowned-text').textContent = 'One payment of ' + o.price.display + '. ' + (o.price.note || '') + ' Your licence key and downloads appear here straight after.';
      if (params.get('buy')) { history.replaceState(null, '', '/account'); params.delete('buy'); startBuy(); }
    });
  }

  function startBuy() {
    var buy = $('buy');
    buy.disabled = true;
    say($('buy-msg'), 'Opening the checkout…');
    M.checkout($('code').value).catch(function (e) { buy.disabled = false; say($('buy-msg'), e.message, 'err'); });
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
        $('seats').textContent = 'Computers using this key: ' + list.length + ' of ' + row.max_devices;
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

  /* The files, from jamesmireku.com's own Worker: it checks that this
     account owns MIREKU (or is on the team), then hands back a link that
     works for a day and can be resumed, which a 5.8 GB library needs. */
  var FILES = [
    { id: 'windows', label: 'Windows installer', size: '44 MB' },
    { id: 'mac',     label: 'Mac installer',     size: '136 MB' },
    { id: 'library', label: 'Sound library', size: '5.8 GB' },
    { id: 'guide',   label: 'Installation guide', size: '9 MB' },
    { id: 'readme',  label: 'Read me', size: '3 MB' }
  ];
  /* One clear button for this computer, and the single files tucked away
     underneath for anyone who needs just one. */
  function downloads() {
    var box = $('downloads'), msgEl = $('dl-msg');
    $('get').hidden = false;
    box.innerHTML = '';
    var ua = navigator.userAgent;
    var plat = /Windows/i.test(ua) ? 'windows'
             : /Macintosh|Mac OS X/i.test(ua) && !/iPhone|iPad|iPod/i.test(ua) && !(navigator.maxTouchPoints > 1) ? 'mac' : '';
    if (plat) {
      var all = document.createElement('button');
      all.className = 'btn dl-all'; all.type = 'button';
      all.textContent = 'Download for ' + (plat === 'windows' ? 'Windows' : 'Mac');
      all.addEventListener('click', function () { downloadAll([plat, 'library', 'guide', 'readme'], all, msgEl); });
      box.appendChild(all);
      var note = document.createElement('p');
      note.className = 'muted small-note';
      note.textContent = 'The installer, the sound library (5.8 GB), the installation guide and the read me. Use Wi-Fi if you can.';
      box.appendChild(note);
    } else {
      var p = document.createElement('p');
      p.className = 'muted';
      p.textContent = 'MIREKU runs on Windows and Mac. Open this page on your computer to download it.';
      box.appendChild(p);
    }
    var d = document.createElement('details'); d.className = 'more';
    var sm = document.createElement('summary'); sm.textContent = 'Download files separately'; d.appendChild(sm);
    var ul = document.createElement('ul'); ul.className = 'filelist';
    FILES.forEach(function (f) {
      var li = document.createElement('li');
      var b = document.createElement('button'); b.type = 'button'; b.className = 'linkish';
      b.textContent = f.label;
      b.addEventListener('click', function () { download(f.id, b, msgEl); });
      var sz = document.createElement('span'); sz.className = 'muted'; sz.textContent = f.size;
      li.appendChild(b); li.appendChild(sz); ul.appendChild(li);
    });
    d.appendChild(ul); box.appendChild(d);
    say(msgEl, '');
  }

  /* Starts one download without leaving the page. */
  function start(o) {
    var a = document.createElement('a');
    a.href = o.url; a.download = o.name; a.style.display = 'none';
    document.body.appendChild(a); a.click(); a.remove();
  }

  function ticket(file) {
    return fetch('/api/download', {
      method: 'POST',
      headers: { 'content-type': 'application/json', Authorization: 'Bearer ' + session.access_token },
      body: JSON.stringify({ file: file })
    }).then(function (r) { return r.json().catch(function () { return {}; }); });
  }

  /* Asks for a link to each file first, then starts them one after another.
     A browser may ask once whether this site may download several files. */
  function downloadAll(files, b, msgEl) {
    b.disabled = true;
    say(msgEl, 'Preparing your downloads…');
    Promise.all(files.map(ticket)).then(function (outs) {
      b.disabled = false;
      var bad = outs.filter(function (o) { return !o.url; })[0];
      if (bad) return say(msgEl, (bad.error && bad.error.message) || 'The downloads could not be started. Try again shortly.', 'err');
      outs.forEach(function (o, i) {
        setTimeout(function () {
          start(o);
        }, i * 1200);
      });
      say(msgEl, outs.length + ' downloads are starting. If your browser asks whether this site may download several files, choose Allow. If one is missing, use its own button.', 'ok');
    }).catch(function () { b.disabled = false; say(msgEl, 'The downloads could not be started. Check your connection.', 'err'); });
  }

  function download(file, b, msgEl) {
    b.disabled = true;
    say(msgEl, 'Preparing your download…');
    ticket(file)
      .then(function (out) {
        b.disabled = false;
        if (!out.url) return say(msgEl, (out.error && out.error.message) || 'The download could not be started. Try again shortly.', 'err');
        say(msgEl, 'Your download of ' + out.name + ' is starting. If it stops, press the button again to carry on.', 'ok');
        start(out);
      })
      .catch(function () { b.disabled = false; say(msgEl, 'The download could not be started. Check your connection.', 'err'); });
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
