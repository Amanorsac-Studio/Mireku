/* =====================================================================
   jamesmireku.com: the one piece of code that runs on a server.

   Every page, image and font is a file, served by Cloudflare before this
   script is ever asked. This answers only what no file can:

     POST /api/auth-email   MIREKU's own sign-up, password-reset and
                            sign-in emails, sent from jamesmireku.com.
     POST /api/hit          one page view or one named event, for the
                            team's dashboard (jamesmireku.com/admin).
     POST /api/download     a signed link to an installer or the sound
                            library, for an account that owns MIREKU.
     GET  /download/<file>  the file itself, from the R2 bucket, resumable.
     POST /api/announce     the launch email to the waiting list, sent by
                            a member of the team from the dashboard.

   WHY IT ARRIVES HERE. MIREKU shares its Supabase project, and so its
   accounts, with amanorsac.studio, and a Supabase project hands its
   emails to ONE address: amanorsac.studio/api/auth-email. That code
   passes a request on to this one, unchanged, when the person started
   on jamesmireku.com (the email's return address is on this site).
   Everyone else keeps getting the Amanorsac Studio email.

   WHO MAY ASK. Only Supabase. The request still carries Supabase's own
   signature (Standard Webhooks: HMAC-SHA256 over "id.timestamp.body"),
   and this checks it with the same secret, AUTH_HOOK_SECRET, before it
   sends anything. A request that does not verify, is more than five
   minutes old, or would send someone a link back to another site, is
   refused. If this fails for any reason, amanorsac.studio sends its own
   email instead, so nobody is left without their link.

   Secrets (Cloudflare -> mireku -> Settings -> Variables and Secrets):
     RESEND_API_KEY    the jamesmireku.com Resend account, sending only
     AUTH_HOOK_SECRET  the same value as Supabase's Send Email hook secret
     DOWNLOAD_SECRET   any long random value; signs download links
   Binding (wrangler.jsonc): DOWNLOADS, the amanorsac-downloads R2 bucket.
   Never in this file, never in the repository.
   ===================================================================== */

const SITE = 'https://jamesmireku.com';
const SUPABASE_URL = 'https://kdxckigyhpnwhwgjdgqq.supabase.co';
const FROM = 'MIREKU <hello@jamesmireku.com>';
const REPLY_TO = 'hello@amanorsac.studio';

const LETTERS = {
  signup: {
    subject: 'Confirm your email for MIREKU', heading: 'One press and you are in.',
    intro: 'Thank you for making a MIREKU account. Press the button to confirm your email. Your licence key and downloads will live on your account page at jamesmireku.com.',
    label: 'Confirm my email',
    fine: 'Your account is with Amanorsac Studio, who make MIREKU with James Mireku, so the same email and password also open amanorsac.studio. If you did not make an account, ignore this and nothing more will come.'
  },
  recovery: {
    subject: 'Reset your MIREKU password', heading: 'Set a new password.',
    intro: 'Somebody asked to reset the password for this address on jamesmireku.com. If that was you, press the button and choose a new one.',
    label: 'Choose a new password',
    fine: 'The link works once, for one hour. The same password also opens amanorsac.studio. If you did not ask for this, ignore it: nothing changes until the button is pressed.'
  },
  magiclink: {
    subject: 'Your MIREKU sign-in link', heading: 'Sign in with one press.',
    intro: 'Here is the sign-in link you asked for on jamesmireku.com. It works once, for an hour.',
    label: 'Sign me in', fine: 'If you did not ask for this, ignore it.'
  },
  invite: {
    subject: 'You have been invited to MIREKU', heading: 'You have an account waiting.',
    intro: 'An account has been made for you on jamesmireku.com. Press the button to accept it and choose your password.',
    label: 'Accept the invitation', fine: 'If you were not expecting this, ignore it.'
  },
  email_change: {
    subject: 'Confirm your new email for MIREKU', heading: 'Confirm your new address.',
    intro: 'You asked to change the email on your MIREKU account. Press the button to confirm the new address.',
    label: 'Confirm the new address', fine: 'If you did not ask for this, ignore it and your address stays as it was.'
  },
  reauthentication: {
    subject: 'Your MIREKU code', heading: 'Your code.',
    intro: 'Type this code where jamesmireku.com asks for it. It works for a few minutes.', label: null, fine: ''
  }
};

const json = (obj, status) => new Response(JSON.stringify(obj), { status: status || 200, headers: { 'content-type': 'application/json' } });
const fail = (status, message) => json({ error: { http_code: status, message } }, status);
const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const looksLikeEmail = s => /^[^@\s]+@[^@\s]+\.[^@\s]+$/.test(s) && s.length <= 254;

/* Constant-time comparison: how long it takes must not say how close a guess was. */
function same(a, b) {
  a = String(a); b = String(b);
  let d = a.length ^ b.length;
  for (let i = 0; i < Math.max(a.length, b.length); i++) d |= (a.charCodeAt(i) || 0) ^ (b.charCodeAt(i) || 0);
  return d === 0;
}

/** True when `redirect` sends the person back to this site, and nowhere else. */
export function isOurs(redirect) {
  try {
    const u = new URL(String(redirect || ''));
    return u.protocol === 'https:' && (u.hostname === 'jamesmireku.com' || u.hostname === 'www.jamesmireku.com');
  } catch (e) { return false; }
}

export async function verifySignature(secret, headers, raw, nowSeconds) {
  const id = headers.get('webhook-id') || '', ts = headers.get('webhook-timestamp') || '';
  const sigs = (headers.get('webhook-signature') || '').split(' ').filter(Boolean).map(s => s.replace(/^v1,/, ''));
  if (!secret || !id || !ts || !sigs.length) return false;
  if (!/^\d+$/.test(ts) || Math.abs(nowSeconds - Number(ts)) > 300) return false;
  const keyBytes = Uint8Array.from(atob(secret.replace(/^(v1,)?whsec_/, '')), c => c.charCodeAt(0));
  const key = await crypto.subtle.importKey('raw', keyBytes, { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const mac = new Uint8Array(await crypto.subtle.sign('HMAC', key, new TextEncoder().encode(id + '.' + ts + '.' + raw)));
  const want = btoa(String.fromCharCode.apply(null, mac));
  return sigs.some(s => same(s, want));
}

/* The letter, in MIREKU's colours: near-black, gold, the badge at the top.
   Tables and inline styles, because that is all an email client reads. */
export function letterHtml(o) {
  const font = "font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;";
  const paras = (t, style) => String(t || '').trim().split(/\n\n+/).filter(Boolean)
    .map(p => '<p style="margin:0 0 16px;' + style + '">' + esc(p).replace(/\n/g, '<br>') + '</p>').join('');
  return '<!doctype html><html lang="en-GB"><head><meta charset="utf-8">' +
    '<meta name="viewport" content="width=device-width,initial-scale=1">' +
    '<meta name="color-scheme" content="dark"><meta name="supported-color-schemes" content="dark">' +
    '<title>' + esc(o.subject) + '</title></head>' +
    '<body style="margin:0;padding:0;background:#0a0907;' + font + '-webkit-font-smoothing:antialiased">' +
    '<div style="display:none;max-height:0;overflow:hidden;opacity:0;color:#0a0907">' + esc(o.preheader) + '</div>' +
    '<table role="presentation" width="100%" cellpadding="0" cellspacing="0" border="0" style="background:#0a0907">' +
    '<tr><td align="center" style="padding:30px 12px 40px">' +
    '<table role="presentation" width="560" cellpadding="0" cellspacing="0" border="0" style="width:560px;max-width:100%">' +
    '<tr><td align="center" style="padding:0 0 22px"><a href="' + SITE + '" style="text-decoration:none">' +
      '<img src="' + SITE + '/images/email/mireku-badge.png" width="140" height="113" alt="MIREKU" ' +
      'style="display:block;width:140px;height:113px;border:0;border-radius:10px"></a></td></tr>' +
    '<tr><td style="background:#15120d;border:1px solid #3a2f1c;border-radius:16px;padding:34px 30px 22px">' +
      '<p style="margin:0 0 12px;font-size:12px;letter-spacing:.18em;text-transform:uppercase;font-weight:700;color:#d9a441">' + esc(o.eyebrow || 'MIREKU · Your account') + '</p>' +
      '<h1 style="margin:0 0 18px;font-size:27px;line-height:1.25;font-weight:700;color:#f3c96f">' + esc(o.heading) + '</h1>' +
      paras(o.intro, 'font-size:16.5px;line-height:1.65;color:#efe6d4') +
      (o.code ? '<p style="margin:4px 0 22px;font-size:30px;letter-spacing:.2em;font-weight:700;color:#f3c96f">' + esc(o.code) + '</p>' : '') +
      (o.cta ? '<table role="presentation" cellpadding="0" cellspacing="0" border="0" style="margin:8px 0 26px"><tr>' +
        '<td style="border-radius:10px;background:#d9a441">' +
        '<a href="' + esc(o.cta.href) + '" style="display:inline-block;padding:15px 30px;font-size:16px;font-weight:700;color:#1a1206;text-decoration:none;border-radius:10px">' +
        esc(o.cta.label) + '</a></td></tr></table>' : '') +
      paras(o.fine, 'font-size:13.5px;line-height:1.55;color:#a69a86') +
    '</td></tr>' +
    '<tr><td align="center" style="padding:22px 10px 0;font-size:12.5px;line-height:1.6;color:#a69a86">' +
      'MIREKU · James Mireku\'s sounds · made with Amanorsac Studio<br>' +
      '<a href="' + SITE + '" style="color:#d9a441;text-decoration:none">jamesmireku.com</a>' +
    '</td></tr></table></td></tr></table></body></html>';
}

/** Builds the email for one Supabase hook payload, or null when it is not ours to send. */
export function buildLetter(body) {
  const user = body.user || {}, d = body.email_data || {};
  const type = String(d.email_action_type || '');
  const letter = LETTERS[type];
  const to = String(type === 'email_change' && d.new_email ? d.new_email : user.email || '').toLowerCase();
  if (!letter || !looksLikeEmail(to) || !isOurs(d.redirect_to)) return null;
  const hash = type === 'email_change' && d.token_hash_new ? d.token_hash_new : d.token_hash;
  const link = SUPABASE_URL + '/auth/v1/verify?token=' + encodeURIComponent(hash || '') +
               '&type=' + encodeURIComponent(type) + '&redirect_to=' + encodeURIComponent(d.redirect_to);
  const code = letter.label ? '' : String(d.token || '');
  const text = letter.intro + '\n\n' + (letter.label ? link : 'Your code: ' + code) + '\n\n' +
               (letter.fine ? letter.fine + '\n\n' : '') + 'MIREKU · made with Amanorsac Studio\n' + SITE + '\n';
  const html = letterHtml({ subject: letter.subject, preheader: letter.intro.slice(0, 120), heading: letter.heading,
                            intro: letter.intro, code, cta: letter.label ? { href: link, label: letter.label } : null, fine: letter.fine });
  return { from: FROM, to: [to], reply_to: REPLY_TO, subject: letter.subject, text, html };
}

async function authEmail(request, env) {
  if (!env.RESEND_API_KEY || !env.AUTH_HOOK_SECRET) return fail(503, 'MIREKU emails are not set up yet.');
  const raw = await request.text();
  if (!(await verifySignature(env.AUTH_HOOK_SECRET, request.headers, raw, Date.now() / 1000))) return fail(401, 'Bad signature.');
  let body;
  try { body = JSON.parse(raw); } catch (e) { return fail(400, 'Bad body.'); }
  const msg = buildLetter(body);
  if (!msg) return fail(400, 'Not a MIREKU email.');
  const r = await fetch('https://api.resend.com/emails', {
    method: 'POST',
    headers: { 'content-type': 'application/json', Authorization: 'Bearer ' + env.RESEND_API_KEY },
    body: JSON.stringify(msg)
  });
  if (!r.ok) {
    console.error('MIREKU email failed:', body.email_data && body.email_data.email_action_type, r.status, await r.text());
    return fail(502, 'The email could not be sent.');
  }
  return json({});
}

/* ---------------------------------------------------------------------
   Visits, for the dashboard. The page sends what it knows (the path, the
   site it came from, phone or computer, a random id for this browser tab);
   this adds the country from the connection, which a page cannot know,
   and writes one row to Supabase with the public key. The tables accept
   new rows and nothing else (studio/supabase-mireku-dashboard.sql). No IP
   address, cookie or name is stored. Bots are not counted.
   --------------------------------------------------------------------- */
const SUPABASE_KEY = 'sb_publishable_PlVBmRgFdhTkVMurXLiBFQ_NjiVssQp';   // public on purpose, like the pages'
const EVENTS = new Set(['notify_signup', 'buy_click', 'release_play', 'account_created']);
const BOT = /bot|crawl|spider|slurp|preview|facebookexternalhit|whatsapp|headless|lighthouse|monitor/i;
const clean = (v, n) => typeof v === 'string' ? v.replace(/[\u0000-\u001f]/g, '').trim().slice(0, n) : '';

export function hitRow(b, country) {
  const sid = clean(b.sid, 64);
  if (sid.length < 8) return null;
  if (b.t === 'view') {
    const path = clean(b.path, 200);
    if (!path.startsWith('/') || path.startsWith('/admin')) return null;
    const ref = clean(b.ref, 120).toLowerCase().replace(/^www\./, '');
    return { table: 'mireku_page_views', row: {
      path, session_id: sid,
      ref_host: ref && ref !== 'jamesmireku.com' ? ref : null,
      device: ['phone', 'tablet', 'computer'].includes(b.device) ? b.device : null,
      country: /^[A-Z]{2}$/.test(country || '') && country !== 'XX' && country !== 'T1' ? country : null } };
  }
  if (b.t === 'event' && EVENTS.has(b.name)) {
    return { table: 'mireku_events', row: { name: b.name, detail: clean(b.detail, 120) || null, session_id: sid } };
  }
  return null;
}

async function hit(request, ctx) {
  const done = new Response(null, { status: 204 });
  if (BOT.test(request.headers.get('user-agent') || '')) return done;
  let b;
  try { b = await request.json(); } catch (e) { return done; }
  const h = hitRow(b || {}, request.cf && request.cf.country);
  if (!h) return done;
  const write = fetch(SUPABASE_URL + '/rest/v1/' + h.table, {
    method: 'POST',
    headers: { 'content-type': 'application/json', apikey: SUPABASE_KEY, Authorization: 'Bearer ' + SUPABASE_KEY, Prefer: 'return=minimal' },
    body: JSON.stringify(h.row)
  }).then(r => { if (!r.ok) return r.text().then(t => console.error('hit refused:', r.status, t)); })
    .catch(e => console.error('hit failed:', e && e.message));
  // The visitor never waits for a statistic.
  if (ctx && ctx.waitUntil) ctx.waitUntil(write); else await write;
  return done;
}

/* ---------------------------------------------------------------------
   Downloads. The files sit in the studio's R2 bucket, amanorsac-downloads,
   under MIREKU/. A new release is a new file there and a new line here.
   --------------------------------------------------------------------- */
export const FILES = {
  windows: { key: 'MIREKU/Mireku-1.2.1-Windows.exe', name: 'Mireku-1.2.1-Windows.exe', type: 'application/vnd.microsoft.portable-executable', label: 'Windows installer', version: '1.2.1' },
  mac:     { key: 'MIREKU/Mireku-1.2.1-macOS.pkg',   name: 'Mireku-1.2.1-macOS.pkg',   type: 'application/octet-stream', label: 'Mac installer', version: '1.2.1' },
  library: { key: 'MIREKU/MIREKU-Library.mirekupack', name: 'MIREKU-Library.mirekupack', type: 'application/octet-stream', label: 'Sound library', version: '1.0' },
  guide:   { key: 'MIREKU/MIREKU-Installation-Guide.pdf', name: 'MIREKU-Installation-Guide.pdf', type: 'application/pdf', label: 'Installation guide', version: '1.2.1' },
  readme:  { key: 'MIREKU/MIREKU-Read-Me.pdf', name: 'MIREKU-Read-Me.pdf', type: 'application/pdf', label: 'Read me', version: '1.2.1' }
};
const TICKET_HOURS = 24;   // long enough to finish, and to resume, a 5.8 GB download on a slow line

async function hmac(secret, text) {
  const key = await crypto.subtle.importKey('raw', new TextEncoder().encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const mac = new Uint8Array(await crypto.subtle.sign('HMAC', key, new TextEncoder().encode(text)));
  return btoa(String.fromCharCode.apply(null, mac)).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}
export async function signTicket(secret, file, expires) {
  return '/download/' + file + '?e=' + expires + '&s=' + (await hmac(secret, 'download:' + file + ':' + expires));
}
export async function checkTicket(secret, file, e, s, nowMs) {
  if (!secret || !FILES[file] || !/^\d{10,14}$/.test(e || '') || Number(e) < nowMs) return false;
  return same(s || '', await hmac(secret, 'download:' + file + ':' + e));
}

/* Asks Supabase, as the signed-in person, whether they may have MIREKU:
   they own it (has_app_access), or they are on the team. */
async function mayDownload(jwt) {
  const ask = (fn, body) => fetch(SUPABASE_URL + '/rest/v1/rpc/' + fn, {
    method: 'POST',
    headers: { 'content-type': 'application/json', apikey: SUPABASE_KEY, Authorization: 'Bearer ' + jwt },
    body: JSON.stringify(body)
  }).then(async r => ({ status: r.status, ok: r.ok, value: r.ok ? await r.json() : null }));
  const owns = await ask('has_app_access', { p_app: 'mireku' });
  if (owns.status === 401) return 'sign_in';
  if (owns.ok && owns.value === true) return 'owner';
  const team = await ask('is_mireku_admin', {});
  if (team.ok && team.value === true) return 'team';
  return owns.ok ? 'no' : 'error';
}

function claimsFrom(jwt) {
  try {
    const p = String(jwt).split('.')[1].replace(/-/g, '+').replace(/_/g, '/');
    const bytes = Uint8Array.from(atob(p + '='.repeat((4 - p.length % 4) % 4)), c => c.charCodeAt(0));
    return JSON.parse(new TextDecoder().decode(bytes)) || {};
  } catch (e) { return {}; }
}

function userIdFrom(jwt) {
  try {
    const p = String(jwt).split('.')[1].replace(/-/g, '+').replace(/_/g, '/');
    const sub = JSON.parse(atob(p + '='.repeat((4 - p.length % 4) % 4))).sub;
    return /^[0-9a-f-]{36}$/i.test(sub || '') ? sub : null;
  } catch (e) { return null; }
}

async function downloadTicket(request, env, ctx) {
  if (!env.DOWNLOADS || !env.DOWNLOAD_SECRET) return fail(503, 'Downloads are not set up yet.');
  const jwt = (request.headers.get('authorization') || '').replace(/^Bearer\s+/i, '').trim();
  if (!jwt) return fail(401, 'Sign in to download MIREKU.');
  let body = {};
  try { body = await request.json(); } catch (e) {}
  const file = String(body.file || '');
  if (!FILES[file]) return fail(404, 'No such download.');
  const who = await mayDownload(jwt);
  if (who === 'sign_in') return fail(401, 'Your session has ended. Sign in again.');
  if (who === 'error') return fail(502, 'Your account could not be checked just now. Try again shortly.');
  if (who === 'no') return fail(403, 'This account does not own MIREKU yet.');
  const url = await signTicket(env.DOWNLOAD_SECRET, file, Date.now() + TICKET_HOURS * 3600 * 1000);
  // Counted when the link is issued: one deliberate press, not every resumed piece.
  const row = { file, user_id: userIdFrom(jwt), country: /^[A-Z]{2}$/.test((request.cf && request.cf.country) || '') ? request.cf.country : null };
  const count = fetch(SUPABASE_URL + '/rest/v1/mireku_downloads', {
    method: 'POST',
    headers: { 'content-type': 'application/json', apikey: SUPABASE_KEY, Authorization: 'Bearer ' + jwt, Prefer: 'return=minimal' },
    body: JSON.stringify(row)
  }).then(r => { if (!r.ok) return r.text().then(t => console.error('download count refused:', r.status, t)); })
    .catch(e => console.error('download count failed:', e && e.message));
  if (ctx && ctx.waitUntil) ctx.waitUntil(count);
  const f = FILES[file];
  return json({ url, name: f.name, label: f.label, version: f.version });
}

/* The file, straight from R2. Ranges are honoured, so a browser or a
   download manager can resume a dropped 5.8 GB download where it stopped. */
async function serveFile(request, env, file, url) {
  if (!env.DOWNLOADS || !env.DOWNLOAD_SECRET) return fail(503, 'Downloads are not set up yet.');
  if (!(await checkTicket(env.DOWNLOAD_SECRET, file, url.searchParams.get('e'), url.searchParams.get('s'), Date.now()))) {
    return new Response('This download link has expired. Go back to jamesmireku.com/account and press the button again.',
                        { status: 403, headers: { 'content-type': 'text/plain; charset=utf-8' } });
  }
  const f = FILES[file];
  const obj = await env.DOWNLOADS.get(f.key, { range: request.headers, onlyIf: request.headers });
  if (obj === null) return new Response('That file is not available just now.', { status: 404 });
  const headers = new Headers();
  obj.writeHttpMetadata(headers);
  headers.set('content-type', f.type);
  headers.set('content-disposition', 'attachment; filename="' + f.name + '"');
  headers.set('etag', obj.httpEtag);
  headers.set('accept-ranges', 'bytes');
  headers.set('cache-control', 'private, no-store');
  if (!('body' in obj) || !obj.body) return new Response(null, { status: 304, headers });   // a precondition said "unchanged"
  if (obj.range && request.headers.has('range')) {
    const start = obj.range.offset || 0;
    const len = obj.range.length != null ? obj.range.length : obj.size - start;
    headers.set('content-range', 'bytes ' + start + '-' + (start + len - 1) + '/' + obj.size);
    headers.set('content-length', String(len));
    return new Response(request.method === 'HEAD' ? null : obj.body, { status: 206, headers });
  }
  headers.set('content-length', String(obj.size));
  return new Response(request.method === 'HEAD' ? null : obj.body, { status: 200, headers });
}

/* ---------------------------------------------------------------------
   The launch email to the waiting list.

   Sent only when a member of the MIREKU team presses the button on the
   dashboard. The list is read AS that person (mireku_interest_list answers
   the team only), so this endpoint can reach nobody's details by itself.
   Only people who gave an email and ticked "message me about MIREKU" are
   on the list at all. Each address gets the email once: what was sent is
   written to mireku_mail_sent, and a second press skips those addresses.
   "test" sends one copy to the person pressing the button and nobody else.
   --------------------------------------------------------------------- */
export const CAMPAIGN = { id: 'launch-1', code: 'MIREKU20', price: '$119', after: '$99' };

export function launchLetter(to, name) {
  const first = String(name || '').trim().split(/\s+/)[0];
  const hello = first ? 'Hello ' + first + ',' : 'Hello,';
  const link = SITE + '/?utm_source=waitlist#buy';
  const intro = hello + '\n\nYou asked us to tell you when MIREKU is out. It is. James Mireku\'s keyboard sounds in one instrument: ' +
    '78 instruments and 22 of his own performances, for Windows and Mac.\n\n' +
    'Thank you for waiting. Here is $20 off: the launch price is ' + CAMPAIGN.price + ', and with your code it is ' + CAMPAIGN.after + '.';
  const fine = 'Type the code in the Discount code box before you press Buy. ' +
    'You are getting this because you asked jamesmireku.com to tell you when MIREKU is out. Reply to this email and we will take you off the list.';
  const subject = 'MIREKU is out: your $20 off code';
  return {
    from: FROM, to: [to], reply_to: REPLY_TO, subject,
    headers: { 'List-Unsubscribe': '<mailto:' + REPLY_TO + '?subject=Remove%20me%20from%20the%20MIREKU%20list>' },
    text: intro + '\n\nYour code: ' + CAMPAIGN.code + '\n\n' + link + '\n\n' + fine + '\n\nMIREKU · made with Amanorsac Studio\n' + SITE + '\n',
    html: letterHtml({ subject, preheader: '$20 off for everyone who waited: ' + CAMPAIGN.after + ' with code ' + CAMPAIGN.code + '.',
                       eyebrow: 'MIREKU · It is out', heading: 'MIREKU is here.', intro, code: CAMPAIGN.code,
                       cta: { href: link, label: 'Get MIREKU' }, fine })
  };
}

/** The people to write to: an email, once each, not already sent this campaign. */
export function recipients(rows, alreadySent) {
  const seen = new Set((alreadySent || []).map(e => String(e).toLowerCase()));
  const out = [];
  for (const r of rows || []) {
    const email = String(r.email || '').trim().toLowerCase();
    if (!looksLikeEmail(email) || seen.has(email)) continue;
    seen.add(email);
    out.push({ email, name: r.name });
  }
  return out;
}

async function announce(request, env) {
  if (!env.RESEND_API_KEY) return fail(503, 'MIREKU emails are not set up yet.');
  const jwt = (request.headers.get('authorization') || '').replace(/^Bearer\s+/i, '').trim();
  if (!jwt) return fail(401, 'Sign in first.');
  let body = {};
  try { body = await request.json(); } catch (e) {}
  const sb = (path, init) => fetch(SUPABASE_URL + path, Object.assign({}, init, {
    headers: Object.assign({ 'content-type': 'application/json', apikey: SUPABASE_KEY, Authorization: 'Bearer ' + jwt }, (init && init.headers) || {}) }));

  const team = await sb('/rest/v1/rpc/is_mireku_admin', { method: 'POST', body: '{}' });
  if (team.status === 401) return fail(401, 'Your session has ended. Sign in again.');
  if (!team.ok || (await team.json()) !== true) return fail(403, 'This is for the MIREKU team.');

  const resend = (path, payload) => fetch('https://api.resend.com' + path, {
    method: 'POST', headers: { 'content-type': 'application/json', Authorization: 'Bearer ' + env.RESEND_API_KEY }, body: JSON.stringify(payload) });

  if (body.mode === 'test') {
    // Read from the sign-in token itself. It is genuine: Supabase checked its
    // signature a moment ago, when it answered is_mireku_admin for it.
    const who = claimsFrom(jwt);
    if (!looksLikeEmail(String(who.email || ''))) return fail(400, 'Your account has no email address.');
    const r = await resend('/emails', launchLetter(String(who.email).toLowerCase(), (who.user_metadata && who.user_metadata.full_name) || ''));
    if (!r.ok) { const why = await resendReason(r); console.error('test announce failed:', why); return fail(502, 'The test email could not be sent. ' + why); }
    return json({ ok: true, test: true, to: who.email });
  }
  if (body.mode !== 'send' && body.mode !== 'count') return fail(400, 'Say what to do: count, test or send.');

  const list = await sb('/rest/v1/rpc/mireku_interest_list', { method: 'POST', body: '{}' });
  if (!list.ok) return fail(502, 'The waiting list could not be read.');
  const sentRes = await sb('/rest/v1/mireku_mail_sent?campaign=eq.' + CAMPAIGN.id + '&select=email', { method: 'GET' });
  if (!sentRes.ok) return fail(503, 'Run the updated studio/supabase-mireku-dashboard.sql in Supabase first.');
  const rows = await list.json(), sent = (await sentRes.json()).map(x => x.email);
  const todo = recipients(rows, sent);
  const withEmail = recipients(rows, []).length;
  if (body.mode === 'count') return json({ ok: true, waiting: rows.length, with_email: withEmail, already_sent: withEmail - todo.length, to_send: todo.length });

  /* Resend refuses a whole batch if one address in it is unacceptable. So a
     refused batch is split in half and each half tried again, down to the
     single address at fault, which is reported and skipped. Each batch that
     is accepted is recorded at once, so nothing is ever sent twice. Calls
     are spaced out to stay inside Resend's two-requests-a-second limit. */
  const wait = ms => new Promise(r => setTimeout(r, ms));
  let delivered = 0, calls = 0, stop = '';
  const bad = [];
  async function sendPart(part) {
    if (stop || !part.length) return;
    if (calls++) await wait(600);
    let r = await resend('/emails/batch', part.map(p => launchLetter(p.email, p.name)));
    if (r.status === 429) { await wait(1500); r = await resend('/emails/batch', part.map(p => launchLetter(p.email, p.name))); }
    if (r.ok) {
      delivered += part.length;
      const log = await sb('/rest/v1/mireku_mail_sent', { method: 'POST', headers: { Prefer: 'return=minimal,resolution=ignore-duplicates' },
        body: JSON.stringify(part.map(p => ({ campaign: CAMPAIGN.id, email: p.email }))) });
      if (!log.ok) console.error('announce log refused:', log.status, await log.text());
      return;
    }
    const why = await resendReason(r);
    console.error('announce batch refused:', r.status, why, part.length);
    if (r.status !== 400 && r.status !== 422) { stop = why; return; }       // not about an address: stop, say why
    if (part.length === 1) { bad.push({ email: part[0].email, reason: why }); return; }
    const mid = Math.ceil(part.length / 2);
    await sendPart(part.slice(0, mid));
    await sendPart(part.slice(mid));
  }
  for (let i = 0; i < todo.length && !stop; i += 100) await sendPart(todo.slice(i, i + 100));
  const failed = todo.length - delivered;
  return json({ ok: failed === 0, sent: delivered, failed, refused: bad.slice(0, 20), reason: stop || (bad[0] && bad[0].reason) || '',
                already_sent: withEmail - todo.length });
}

/** Resend's own words for a refusal, shortened; safe to show the team. */
async function resendReason(r) {
  let t = '';
  try { t = await r.text(); } catch (e) {}
  try { const j = JSON.parse(t); t = j.message || j.error || t; } catch (e) {}
  return ('Resend said (' + r.status + '): ' + String(t).replace(/\s+/g, ' ')).slice(0, 300);
}

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    if (url.pathname === '/api/announce') {
      if (request.method !== 'POST') return fail(405, 'Use POST.');
      try { return await announce(request, env); }
      catch (e) { console.error('announce:', e && e.message); return fail(500, 'Something went wrong.'); }
    }
    if (url.pathname === '/api/download') {
      if (request.method !== 'POST') return fail(405, 'Use POST.');
      try { return await downloadTicket(request, env, ctx); }
      catch (e) { console.error('download:', e && e.message); return fail(500, 'Something went wrong.'); }
    }
    const dl = url.pathname.match(/^\/download\/(windows|mac|library|guide|readme)$/);
    if (dl && (request.method === 'GET' || request.method === 'HEAD')) {
      try { return await serveFile(request, env, dl[1], url); }
      catch (e) { console.error('serve:', e && e.message); return new Response('Something went wrong. Try again.', { status: 500 }); }
    }
    if (url.pathname === '/api/hit') {
      if (request.method !== 'POST') return fail(405, 'Use POST.');
      return hit(request, ctx);
    }
    if (url.pathname === '/api/auth-email') {
      if (request.method !== 'POST') return fail(405, 'Use POST.');
      try { return await authEmail(request, env); }
      catch (e) { console.error('auth-email:', e && e.message); return fail(500, 'Something went wrong.'); }
    }
    // Anything else that no file answered: the 404 page, as before.
    return env.ASSETS.fetch(request);
  }
};
