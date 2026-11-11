(function () {
  /* ---- the four worlds: tabs, with arrow keys between them ---- */
  var tabs = Array.prototype.slice.call(document.querySelectorAll('.tab'));
  function pick(t, focus) {
    tabs.forEach(function (x) {
      var on = x === t;
      x.setAttribute('aria-selected', on ? 'true' : 'false');
      x.tabIndex = on ? 0 : -1;
      document.getElementById(x.getAttribute('aria-controls')).hidden = !on;
    });
    if (focus) t.focus();
  }
  tabs.forEach(function (t, i) {
    t.addEventListener('click', function () { pick(t); });
    t.addEventListener('keydown', function (e) {
      if (e.key === 'ArrowRight') pick(tabs[(i + 1) % tabs.length], true);
      if (e.key === 'ArrowLeft') pick(tabs[(i - 1 + tabs.length) % tabs.length], true);
    });
  });

  /* ---- sections rise in as they arrive ---- */
  var items = document.querySelectorAll('.reveal');
  if ('IntersectionObserver' in window) {
    var io = new IntersectionObserver(function (es) {
      es.forEach(function (e) { if (e.isIntersecting) { e.target.classList.add('in'); io.unobserve(e.target); } });
    }, { rootMargin: '0px 0px -8% 0px' });
    items.forEach(function (el) { io.observe(el); });
  } else {
    items.forEach(function (el) { el.classList.add('in'); });
  }

  /* ---- the buy box: "Coming soon" until both switches say otherwise ---- */
  var btn = document.querySelector('[data-buy]');
  var msg = document.querySelector('[data-buy-msg]');
  var price = document.querySelector('[data-price]');
  var sub = document.querySelector('[data-price-sub]');
  var note = document.querySelector('[data-price-note]');
  var heroLabel = document.querySelector('[data-buy-label]');

  window.Mireku.offer().then(function (o) {
    if (!o.onSale) return;
    price.textContent = '';
    if (o.price.list) { var was = document.createElement('s'); was.className = 'was'; was.textContent = o.price.list; price.appendChild(was); price.appendChild(document.createTextNode(' ')); }
    price.appendChild(document.createTextNode(o.price.display));
    if (o.price.list) price.setAttribute('aria-label', o.price.display + ', was ' + o.price.list);
    sub.textContent = 'One payment.';
    note.textContent = o.price.display + ' · ' + (o.price.note || 'Mobile money, bank transfer or a local card.');
    heroLabel.textContent = 'Buy MIREKU · ' + o.price.display;
    var navBuy = document.querySelector('[data-nav-buy]');
    if (navBuy) navBuy.textContent = 'Get MIREKU';
    var form = document.getElementById('interest');
    if (form) form.hidden = true;               // on sale: the sign-up gives way to the button
    var codeRow = document.querySelector('[data-code-row]');
    if (codeRow) { codeRow.hidden = false; document.getElementById('code').value = window.Mireku.rememberedCode(); }
    btn.hidden = false;
    btn.disabled = false;
    btn.textContent = 'Buy MIREKU';
  });

  btn.addEventListener('click', function () {
    if (window.mirekuTrack) window.mirekuTrack('buy_click');
    btn.disabled = true;
    msg.className = 'msg';
    msg.textContent = 'Opening the checkout…';
    window.Mireku.checkout(document.getElementById('code').value).catch(function (e) {
      btn.disabled = false;
      msg.className = 'msg err';
      msg.textContent = e.message;
    });
  });

  /* ---- "tell me when it's out" ----
     Adds one row to Supabase's mireku_interest table with the public key.
     The table accepts new rows and nothing else (studio/supabase-mireku-
     interest.sql), so this page can write to the list but never read it. */
  var form = document.getElementById('interest');
  if (form) {
    var C = window.MIREKU;
    var imsg = document.getElementById('i-msg');
    var say = function (t, k) { imsg.className = 'msg' + (k ? ' ' + k : ''); imsg.textContent = t; };
    form.addEventListener('submit', function (e) {
      e.preventDefault();
      var name = document.getElementById('i-name').value.trim();
      var phone = document.getElementById('i-phone').value.trim().replace(/\s+/g, ' ');
      var email = document.getElementById('i-email').value.trim();
      var digits = phone.replace(/[^0-9]/g, '');
      if (!name) return say('Enter your name.', 'err');
      if (!/^\+?[0-9][0-9 ()-]{5,22}$/.test(phone) || digits.length < 7 || digits.length > 15)
        return say('Enter a phone number with its country code, such as +233 24 123 4567.', 'err');
      if (email && !/^[^@\s]+@[^@\s]+\.[^@\s]+$/.test(email)) return say('That email address does not look right.', 'err');
      if (!document.getElementById('i-ok').checked) return say('Tick the box so we may message you.', 'err');
      var submit = form.querySelector('button[type=submit]');
      if (document.getElementById('i-web').value) { form.reset(); return say('Thank you. You are on the list.', 'ok'); }   // a bot filled the hidden field
      submit.disabled = true;
      say('Saving…');
      fetch(C.SUPABASE_URL + '/rest/v1/mireku_interest', {
        method: 'POST',
        headers: { 'content-type': 'application/json', apikey: C.SUPABASE_KEY,
                   Authorization: 'Bearer ' + C.SUPABASE_KEY, Prefer: 'return=minimal' },
        body: JSON.stringify({ name: name, phone: phone, email: email || null, consent: true, source: 'jamesmireku.com' })
      }).then(function (r) {
        submit.disabled = false;
        if (r.ok) { form.reset(); if (window.mirekuTrack) window.mirekuTrack('notify_signup'); return say('Thank you, ' + name.split(' ')[0] + '. We will message you when MIREKU is out.', 'ok'); }
        if (r.status === 409) return say('That number is already on the list. We will be in touch.', 'ok');
        say('Your details could not be saved just now. Try again in a few minutes.', 'err');
      }).catch(function () {
        submit.disabled = false;
        say('Your details could not be saved. Check your connection and try again.', 'err');
      });
    });
  }
})();
