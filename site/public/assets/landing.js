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
    price.textContent = o.price.display;
    sub.textContent = (o.price.usd ? 'About ' + o.price.usd + '. ' : '') + 'One payment. ' + (o.price.note || '');
    note.textContent = o.price.display + ' · ' + (o.price.note || 'Mobile money, bank transfer or a local card.');
    heroLabel.textContent = 'Buy MIREKU · ' + o.price.display;
    btn.disabled = false;
    btn.textContent = 'Buy MIREKU';
  });

  btn.addEventListener('click', function () {
    btn.disabled = true;
    msg.className = 'msg';
    msg.textContent = 'Opening the checkout…';
    window.Mireku.checkout().catch(function (e) {
      btn.disabled = false;
      msg.className = 'msg err';
      msg.textContent = e.message;
    });
  });
})();
