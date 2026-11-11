/* =====================================================================
   The MIREKU team dashboard (jamesmireku.com/admin).

   Who sees it is decided by the database, not by this page: the numbers
   come from mireku_dashboard() and mireku_interest_list(), which answer
   only accounts on the mireku_admins list (studio/supabase-mireku-
   dashboard.sql). Anyone else who opens this page sees a polite refusal,
   and the page holds no data of its own to leak.
   ===================================================================== */
(function () {
  var sb = window.Mireku.sb();
  var $ = function (id) { return document.getElementById(id); };
  var days = 30;
  var interest = [];

  function show(id) { ['loading', 'signin', 'denied', 'dash'].forEach(function (x) { $(x).hidden = x !== id; }); }
  function el(tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }
  var fmt = new Intl.NumberFormat('en-GB');
  var countryName = (function () {
    try { var dn = new Intl.DisplayNames(['en-GB'], { type: 'region' }); return function (c) { return c === '??' ? 'Unknown' : (dn.of(c) || c); }; }
    catch (e) { return function (c) { return c === '??' ? 'Unknown' : c; }; }
  })();
  var dayLabel = function (iso, long) {
    var d = new Date(iso + 'T00:00:00Z');
    return d.toLocaleDateString('en-GB', long ? { weekday: 'short', day: 'numeric', month: 'long', timeZone: 'UTC' }
                                             : { day: 'numeric', month: 'short', timeZone: 'UTC' });
  };
  var money = function (cur, cents) {
    try { return new Intl.NumberFormat('en-GB', { style: 'currency', currency: cur, maximumFractionDigits: 0 }).format(cents / 100); }
    catch (e) { return cur + ' ' + fmt.format(Math.round(cents / 100)); }
  };

  if (!sb) { $('loading').textContent = 'The page did not finish loading. Refresh to try again.'; return; }

  /* ---------------- signing in ---------------- */
  $('f-signin').addEventListener('submit', function (e) {
    e.preventDefault();
    var m = $('signin-msg');
    m.className = 'msg'; m.textContent = 'Signing in…';
    sb.auth.signInWithPassword({ email: $('email').value.trim(), password: $('pass').value }).then(function (r) {
      if (r.error) { m.className = 'msg err'; m.textContent = /invalid/i.test(r.error.message) ? 'That email and password do not match an account.' : r.error.message; }
    });
  });
  $('signout').addEventListener('click', function () { sb.auth.signOut(); });

  sb.auth.onAuthStateChange(function (event, session) {
    if (event !== 'INITIAL_SESSION' && event !== 'SIGNED_IN' && event !== 'SIGNED_OUT') return;
    setTimeout(function () { route(session); }, 0);
  });

  var user = null;
  function route(session) {
    $('signout').hidden = !session;
    if (!session) { user = null; return show('signin'); }
    user = session.user;
    show('loading');
    load();
  }

  /* ---------------- the numbers ---------------- */
  document.querySelectorAll('.seg button').forEach(function (b) {
    b.addEventListener('click', function () {
      days = Number(b.dataset.days);
      document.querySelectorAll('.seg button').forEach(function (x) { x.setAttribute('aria-pressed', x === b ? 'true' : 'false'); });
      load();
    });
  });

  function load() {
    Promise.all([sb.rpc('mireku_dashboard', { p_days: days }), sb.rpc('mireku_interest_list')]).then(function (rs) {
      var d = rs[0], l = rs[1];
      var err = d.error || l.error;
      if (err) {
        if (/MIREKU team|permission|42501/i.test(err.message + ' ' + (err.code || ''))) {
          $('denied-who').textContent = user ? user.email : '';
          return show('denied');
        }
        $('loading').textContent = /does not exist|PGRST202/i.test(err.message + ' ' + (err.code || ''))
          ? 'The dashboard is not set up in Supabase yet: run studio/supabase-mireku-dashboard.sql.'
          : 'The numbers could not be loaded: ' + err.message;
        return show('loading');
      }
      interest = l.data || [];
      draw(d.data);
      show('dash');
    });
  }

  function draw(d) {
    $('who').textContent = 'Signed in as ' + (user ? user.email : '') + ' · last ' + d.days + ' days, from ' + dayLabel(String(d.since).slice(0, 10), true);

    // headline tiles
    var rev = (d.revenue || []).map(function (r) { return money(r.currency, r.cents); }).join(' + ') || '0';
    var tiles = [
      ['Visitors', fmt.format(d.visits.visitors), fmt.format(d.visits.views) + ' page views'],
      ['Waiting for MIREKU', fmt.format(d.interest.total), '+' + fmt.format(d.interest.week) + ' this week'],
      ['Sales', fmt.format(d.sales.paid), fmt.format(d.sales.paid_period) + ' in this period'],
      ['Takings', rev, 'all time, before fees'],
      ['Licence keys', fmt.format(d.licences.keys), (d.sales.free ? fmt.format(d.sales.free) + ' given free · ' : '') + (d.sales.trials ? fmt.format(d.sales.trials) + ' trials' : 'no trials')],
      ['Computers using MIREKU', fmt.format(d.licences.computers), 'activated now']
    ];
    var t = $('tiles'); t.innerHTML = '';
    tiles.forEach(function (x) {
      var c = el('div', 'tile'); c.appendChild(el('span', 'tile-label', x[0])); c.appendChild(el('b', 'tile-value', x[1])); c.appendChild(el('span', 'tile-sub', x[2])); t.appendChild(c);
    });

    // charts: one measure each, one axis each
    bars($('chart-visitors'), d.daily, 'visitors', function (r) { return fmt.format(r.visitors) + (r.visitors === 1 ? ' visitor' : ' visitors') + ' · ' + fmt.format(r.views) + ' page views'; });
    bars($('chart-signups'), d.daily, 'signups', function (r) { return fmt.format(r.signups) + (r.signups === 1 ? ' sign-up' : ' sign-ups'); });

    // the same numbers as a table
    var dt = $('daily-table'); dt.innerHTML = '<thead><tr><th scope="col">Day</th><th scope="col">Visitors</th><th scope="col">Page views</th><th scope="col">Sign-ups</th></tr></thead>';
    var tb = el('tbody');
    d.daily.slice().reverse().forEach(function (r) {
      var tr = el('tr'); [dayLabel(r.day, true), fmt.format(r.visitors), fmt.format(r.views), fmt.format(r.signups)].forEach(function (v, i) { tr.appendChild(el(i ? 'td' : 'th', null, v)); }); tb.appendChild(tr);
    });
    dt.appendChild(tb);

    list($('l-pages'), d.pages, function (n) { return n === '/' ? 'Home' : n.replace(/^\//, '').replace(/^./, function (c) { return c.toUpperCase(); }); }, 'views');
    list($('l-sources'), d.sources, function (n) { return n; }, 'visitors');
    list($('l-countries'), d.countries, countryName, 'visitors');
    list($('l-devices'), d.devices, function (n) { return n.charAt(0).toUpperCase() + n.slice(1); }, 'visitors');

    var ev = d.events || {};
    var e = $('events'); e.innerHTML = '';
    [['Joined the waiting list', ev.notify_signup], ['Made an account', ev.account_created], ['Pressed Buy', ev.buy_click], ['Played a release on Spotify', ev.release_play]]
      .forEach(function (x) { var c = el('div', 'tile'); c.appendChild(el('span', 'tile-label', x[0])); c.appendChild(el('b', 'tile-value', fmt.format(x[1] || 0))); e.appendChild(c); });

    drawInterest();
  }

  /* A column chart drawn in SVG: one gold series, a 0 baseline, a light
     grid, day labels kept sparse, and a tooltip on every day. */
  function bars(box, rows, key, tipText) {
    box.innerHTML = '';
    var W = box.clientWidth || 600, H = 200, padL = 34, padB = 26, padT = 10;
    var max = Math.max(1, Math.max.apply(null, rows.map(function (r) { return r[key]; })));
    var step = Math.pow(10, Math.floor(Math.log10(max))); var top = Math.ceil(max / step) * step; if (top < 4) top = 4;
    var plotW = W - padL - 6, plotH = H - padB - padT, band = plotW / rows.length;
    var bw = Math.max(2, Math.min(24, band - 2));
    var ns = 'http://www.w3.org/2000/svg';
    var svg = document.createElementNS(ns, 'svg'); svg.setAttribute('viewBox', '0 0 ' + W + ' ' + H); svg.setAttribute('width', '100%'); svg.setAttribute('height', H);
    svg.setAttribute('role', 'img'); svg.setAttribute('aria-label', box.previousElementSibling.textContent + ': ' + rows.reduce(function (a, r) { return a + r[key]; }, 0) + ' in total');
    var mk = function (n, a) { var x = document.createElementNS(ns, n); for (var k in a) x.setAttribute(k, a[k]); return x; };
    [0, 0.5, 1].forEach(function (f) {
      var y = padT + plotH - f * plotH;
      svg.appendChild(mk('line', { x1: padL, x2: W - 6, y1: y, y2: y, class: f ? 'grid' : 'base' }));
      var t = mk('text', { x: padL - 6, y: y + 4, class: 'axis', 'text-anchor': 'end' }); t.textContent = fmt.format(Math.round(top * f)); svg.appendChild(t);
    });
    var every = Math.ceil(rows.length / 6);
    rows.forEach(function (r, i) {
      var x = padL + i * band + (band - bw) / 2, h = r[key] / top * plotH, y = padT + plotH - h;
      if (h > 0) {
        var rr = Math.min(4, bw / 2, h);
        // rounded at the data end, square at the baseline
        svg.appendChild(mk('path', { class: 'bar', d: 'M' + x + ',' + (padT + plotH) + 'V' + (y + rr) + 'Q' + x + ',' + y + ' ' + (x + rr) + ',' + y +
          'H' + (x + bw - rr) + 'Q' + (x + bw) + ',' + y + ' ' + (x + bw) + ',' + (y + rr) + 'V' + (padT + plotH) + 'Z' }));
      }
      if (i % every === 0 || i === rows.length - 1) {
        var lt = mk('text', { x: x + bw / 2, y: H - 8, class: 'axis', 'text-anchor': 'middle' }); lt.textContent = dayLabel(r.day); svg.appendChild(lt);
      }
      var hitbox = mk('rect', { x: padL + i * band, y: padT, width: band, height: plotH, class: 'hit', tabindex: '0' });
      var label = dayLabel(r.day, true) + ': ' + tipText(r);
      hitbox.setAttribute('aria-label', label);
      var on = function (ev) { tip(label, ev, hitbox); }, off = function () { $('tip').hidden = true; };
      hitbox.addEventListener('mousemove', on); hitbox.addEventListener('focus', on); hitbox.addEventListener('mouseleave', off); hitbox.addEventListener('blur', off);
      svg.appendChild(hitbox);
    });
    box.appendChild(svg);
  }

  function tip(text, ev, target) {
    var t = $('tip'); t.textContent = text; t.hidden = false;
    var r = target.getBoundingClientRect();
    var x = ev && ev.clientX ? ev.clientX : r.left + r.width / 2, y = r.top;
    t.style.left = Math.max(8, Math.min(window.innerWidth - t.offsetWidth - 8, x - t.offsetWidth / 2)) + 'px';
    t.style.top = (y + window.scrollY - t.offsetHeight - 8) + 'px';
  }

  /* A ranked list: name, a gold bar to scale, and the number. */
  function list(box, rows, name, unit) {
    box.innerHTML = '';
    if (!rows || !rows.length) { box.appendChild(el('p', 'muted', 'Nothing yet in this period.')); return; }
    var max = Math.max.apply(null, rows.map(function (r) { return r.n; }));
    rows.forEach(function (r) {
      var row = el('div', 'rank');
      row.appendChild(el('span', 'rank-name', name(r.name)));
      var track = el('span', 'rank-track'); var b = el('span', 'rank-bar'); b.style.width = Math.max(2, r.n / max * 100) + '%'; track.appendChild(b); row.appendChild(track);
      row.appendChild(el('span', 'rank-n', fmt.format(r.n)));
      row.title = name(r.name) + ': ' + fmt.format(r.n) + ' ' + unit;
      box.appendChild(row);
    });
  }

  function drawInterest() {
    var t = $('interest');
    $('i-count').textContent = interest.length ? fmt.format(interest.length) + (interest.length === 1 ? ' person' : ' people') + ', newest first' : '';
    $('csv').disabled = !interest.length;
    t.innerHTML = '';
    if (!interest.length) { t.innerHTML = '<tbody><tr><td class="muted">Nobody yet. Sign-ups from the home page appear here.</td></tr></tbody>'; return; }
    t.innerHTML = '<thead><tr><th scope="col">Signed up</th><th scope="col">Name</th><th scope="col">Phone or WhatsApp</th><th scope="col">Email</th></tr></thead>';
    var tb = el('tbody');
    interest.forEach(function (p) {
      var tr = el('tr');
      tr.appendChild(el('th', null, new Date(p.created_at).toLocaleString('en-GB', { day: 'numeric', month: 'short', year: 'numeric', hour: '2-digit', minute: '2-digit', timeZone: 'UTC' })));
      tr.appendChild(el('td', null, p.name));
      var ph = el('td'); var a = el('a', null, p.phone); a.href = 'https://wa.me/' + String(p.phone).replace(/[^0-9]/g, ''); a.target = '_blank'; a.rel = 'noopener'; a.title = 'Open in WhatsApp'; ph.appendChild(a); tr.appendChild(ph);
      tr.appendChild(el('td', null, p.email || ''));
      tb.appendChild(tr);
    });
    t.appendChild(tb);
  }

  $('csv').addEventListener('click', function () {
    var q = function (v) { v = v == null ? '' : String(v); return /[",\n\r]/.test(v) || /^[=+\-@]/.test(v) ? '"' + v.replace(/^([=+\-@])/, "'$1").replace(/"/g, '""') + '"' : v; };
    var lines = [['Signed up (UTC)', 'Name', 'Phone', 'Email'].join(',')].concat(interest.map(function (p) {
      return [new Date(p.created_at).toISOString().replace('T', ' ').slice(0, 16), p.name, p.phone, p.email].map(q).join(',');
    }));
    var blob = new Blob(['﻿' + lines.join('\r\n')], { type: 'text/csv;charset=utf-8' });
    var a = document.createElement('a'); a.href = URL.createObjectURL(blob);
    a.download = 'mireku-waiting-list-' + new Date().toISOString().slice(0, 10) + '.csv';
    document.body.appendChild(a); a.click(); setTimeout(function () { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
  });

  var resizeTimer;
  window.addEventListener('resize', function () { clearTimeout(resizeTimer); resizeTimer = setTimeout(function () { if (!$('dash').hidden) load(); }, 300); });
})();
