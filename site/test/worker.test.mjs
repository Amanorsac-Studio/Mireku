// Tests for site/worker.js (MIREKU account emails). Run: node site/test/worker.test.mjs
// No network: Resend is imitated, and the Supabase signature is made with a throwaway secret.
import worker, { verifySignature, buildLetter, isOurs, hitRow, signTicket, checkTicket, FILES } from '../worker.js';
import { writeFileSync } from 'node:fs';
const secretBytes = crypto.getRandomValues(new Uint8Array(32));
const secret = 'v1,whsec_' + btoa(String.fromCharCode(...secretBytes));
async function sign(raw, id = 'msg_1', ts = Math.floor(Date.now() / 1000)) {
  const key = await crypto.subtle.importKey('raw', secretBytes, { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const mac = new Uint8Array(await crypto.subtle.sign('HMAC', key, new TextEncoder().encode(id + '.' + ts + '.' + raw)));
  return { 'webhook-id': id, 'webhook-timestamp': String(ts), 'webhook-signature': 'v1,' + btoa(String.fromCharCode(...mac)) };
}
let sent = []; let resendStatus = 200;
globalThis.fetch = async (url, init) => { sent.push({ url, body: JSON.parse(init.body), auth: init.headers.Authorization }); return new Response('{}', { status: resendStatus }); };
const env = { RESEND_API_KEY: 're_test', AUTH_HOOK_SECRET: secret, ASSETS: { fetch: async () => new Response('404 page', { status: 404 }) } };
const payload = (type, redirect, extra = {}) => JSON.stringify({ user: { email: 'Kofi@Example.com' },
  email_data: { email_action_type: type, token_hash: 'abc123', token: '123456', redirect_to: redirect, ...extra } });
async function call(raw, headers, e = env) {
  const r = await worker.fetch(new Request('https://jamesmireku.com/api/auth-email', { method: 'POST', headers: { 'content-type': 'application/json', ...headers }, body: raw }), e);
  return [r.status, await r.text()];
}
let fails = 0; const check = (ok, what) => { console.log((ok ? 'PASS ' : 'FAIL ') + what); if (!ok) fails++; };

let raw = payload('signup', 'https://jamesmireku.com/account');
sent = []; let [st] = await call(raw, await sign(raw));
const m = sent[0] && sent[0].body;
check(st === 200 && sent.length === 1, 'a signed sign-up from jamesmireku.com is sent');
check(m.from === 'MIREKU <hello@jamesmireku.com>' && m.to[0] === 'kofi@example.com' && m.subject === 'Confirm your email for MIREKU', 'from MIREKU, to the person, MIREKU subject');
check(m.text.includes('/auth/v1/verify?token=abc123&type=signup&redirect_to=https%3A%2F%2Fjamesmireku.com%2Faccount') && m.html.includes('/auth/v1/verify?token=abc123&amp;type=signup&amp;redirect_to=https%3A%2F%2Fjamesmireku.com%2Faccount'), 'the button returns them to jamesmireku.com/account');
check(sent[0].auth === 'Bearer re_test' && sent[0].url === 'https://api.resend.com/emails', 'sent through Resend with the MIREKU key');
check(!/!/.test(m.text), 'no exclamation marks in the email text');
if (process.argv.includes('--html')) writeFileSync('signup.html', m.html);

raw = payload('recovery', 'https://jamesmireku.com/account?reset=1'); sent = [];
[st] = await call(raw, await sign(raw)); check(st === 200 && sent[0].body.subject === 'Reset your MIREKU password', 'password reset is sent');
raw = payload('reauthentication', 'https://jamesmireku.com/account'); sent = [];
[st] = await call(raw, await sign(raw)); check(st === 200 && sent[0].body.text.includes('Your code: 123456'), 'a code email carries the code');

raw = payload('signup', 'https://jamesmireku.com/account'); sent = [];
const h = await sign(raw); h['webhook-signature'] = 'v1,' + btoa('forged-signature-forged-signature');
[st] = await call(raw, h); check(st === 401 && sent.length === 0, 'a forged signature is refused and nothing is sent');
[st] = await call(raw.replace('Kofi', 'Mallory'), await sign(raw)); check(st === 401 && sent.length === 0, 'a body changed after signing is refused');
[st] = await call(raw, await sign(raw, 'msg_2', Math.floor(Date.now() / 1000) - 600)); check(st === 401 && sent.length === 0, 'a request older than five minutes is refused');
raw = payload('signup', 'https://amanorsac.studio/client');
[st] = await call(raw, await sign(raw)); check(st === 400 && sent.length === 0, 'a sign-up for another site is not sent from here');
raw = payload('signup', 'https://jamesmireku.com.evil.example/account');
[st] = await call(raw, await sign(raw)); check(st === 400 && sent.length === 0, 'a look-alike address is not ours');
raw = payload('not_a_type', 'https://jamesmireku.com/account');
[st] = await call(raw, await sign(raw)); check(st === 400, 'an unknown email type is refused');
raw = payload('signup', 'https://jamesmireku.com/account');
[st] = await call(raw, await sign(raw), { ...env, RESEND_API_KEY: '' }); check(st === 503, 'without its key it says so instead of failing silently');
resendStatus = 422; [st] = await call(raw, await sign(raw)); check(st === 502, 'a Resend refusal comes back as a failure, so amanorsac.studio can step in'); resendStatus = 200;

let r = await worker.fetch(new Request('https://jamesmireku.com/api/auth-email'), env); check(r.status === 405, 'GET is refused');
r = await worker.fetch(new Request('https://jamesmireku.com/nothing-here'), env); check(r.status === 404 && (await r.text()) === '404 page', 'any other address falls through to the 404 page');
check(isOurs('https://www.jamesmireku.com/x') && !isOurs('http://jamesmireku.com/') && !isOurs('javascript:alert(1)'), 'only https jamesmireku.com counts as ours');
// ---- visits (/api/hit)
let row = hitRow({ t: 'view', path: '/about', ref: 'www.Instagram.com', device: 'phone', sid: 'sess-1234-abcd' }, 'GH');
check(row && row.table === 'mireku_page_views' && row.row.ref_host === 'instagram.com' && row.row.country === 'GH' && row.row.device === 'phone', 'a page view is recorded with source, device and country');
check(hitRow({ t: 'view', path: '/', ref: 'jamesmireku.com', device: 'computer', sid: 'sess-1234-abcd' }, 'XX').row.ref_host === null, 'moving around the site is not counted as a source, and an unknown country is left blank');
check(hitRow({ t: 'view', path: '/admin', sid: 'sess-1234-abcd' }, 'GH') === null, 'the dashboard itself is never counted');
check(hitRow({ t: 'view', path: 'https://evil.example/', sid: 'sess-1234-abcd' }) === null, 'only paths on this site are accepted');
check(hitRow({ t: 'view', path: '/', sid: 'x' }) === null, 'a missing visit id is refused');
check(hitRow({ t: 'event', name: 'notify_signup', sid: 'sess-1234-abcd' }).table === 'mireku_events', 'a named event is recorded');
check(hitRow({ t: 'event', name: 'drop_tables', sid: 'sess-1234-abcd' }) === null, 'an unknown event name is refused');
sent = [];
r = await worker.fetch(new Request('https://jamesmireku.com/api/hit', { method: 'POST', headers: { 'user-agent': 'Mozilla/5.0', 'content-type': 'application/json' },
  body: JSON.stringify({ t: 'view', path: '/', device: 'phone', sid: 'sess-1234-abcd' }) }), env);
check(r.status === 204 && sent.length === 1 && sent[0].url.endsWith('/rest/v1/mireku_page_views') && sent[0].body.path === '/', 'a visit is written to Supabase');
sent = [];
r = await worker.fetch(new Request('https://jamesmireku.com/api/hit', { method: 'POST', headers: { 'user-agent': 'Googlebot/2.1', 'content-type': 'application/json' },
  body: JSON.stringify({ t: 'view', path: '/', sid: 'sess-1234-abcd' }) }), env);
check(r.status === 204 && sent.length === 0, 'bots are not counted');
sent = []; r = await worker.fetch(new Request('https://jamesmireku.com/api/hit', { method: 'POST', body: 'not json' }), env);
check(r.status === 204 && sent.length === 0, 'rubbish is ignored quietly');

// ---- downloads
const bytes = new Uint8Array(1000).map((_, i) => i % 251);
const r2 = { get: async (key, opts) => {
  if (key !== FILES.library.key) return null;
  const h = opts && opts.range; const range = h && h.get && h.get('range');
  let offset = 0, length = bytes.length;
  if (range) { const m = /bytes=(\d+)-(\d*)/.exec(range); offset = +m[1]; length = (m[2] ? +m[2] + 1 : bytes.length) - offset; }
  return { size: bytes.length, httpEtag: '"abc"', range: range ? { offset, length } : undefined,
           body: new Blob([bytes.slice(offset, offset + length)]).stream(), writeHttpMetadata() {} };
} };
const denv = { ...env, DOWNLOADS: r2, DOWNLOAD_SECRET: 'test-secret-for-downloads' };
let owns = true, team = false;
globalThis.fetch = async (url, init) => {
  sent.push({ url, body: init && init.body ? JSON.parse(init.body) : null, auth: init && init.headers && init.headers.Authorization });
  if (String(url).endsWith('/rpc/has_app_access')) return new Response(JSON.stringify(owns), { status: 200 });
  if (String(url).endsWith('/rpc/is_mireku_admin')) return new Response(JSON.stringify(team), { status: 200 });
  return new Response('', { status: 201 });
};
const jwtFor = sub => 'x.' + btoa(JSON.stringify({ sub })).replace(/=+$/, '') + '.y';
const ask = (file, jwt) => worker.fetch(new Request('https://jamesmireku.com/api/download', { method: 'POST',
  headers: { 'content-type': 'application/json', ...(jwt ? { Authorization: 'Bearer ' + jwt } : {}) }, body: JSON.stringify({ file }) }), denv, { waitUntil: p => p });
sent = [];
r = await ask('library', jwtFor('11111111-2222-3333-4444-555555555555')); let out = await r.json();
check(r.status === 200 && /^\/download\/library\?e=\d+&s=/.test(out.url), 'an owner gets a signed link to the library');
check(sent.some(x => String(x.url).endsWith('/rest/v1/mireku_downloads') && x.body.file === 'library' && x.body.user_id === '11111111-2222-3333-4444-555555555555'), 'the download is counted, as that person');
r = await worker.fetch(new Request('https://jamesmireku.com' + out.url), denv);
check(r.status === 200 && r.headers.get('content-length') === '1000' && /attachment; filename="MIREKU-Library.mirekupack"/.test(r.headers.get('content-disposition')), 'the link downloads the whole file with its name');
check(r.headers.get('accept-ranges') === 'bytes', 'it says downloads can be resumed');
r = await worker.fetch(new Request('https://jamesmireku.com' + out.url, { headers: { range: 'bytes=600-' } }), denv);
const part = new Uint8Array(await r.arrayBuffer());
check(r.status === 206 && r.headers.get('content-range') === 'bytes 600-999/1000' && part.length === 400 && part[0] === bytes[600], 'a dropped download resumes from where it stopped');
r = await worker.fetch(new Request('https://jamesmireku.com' + out.url.replace('/library', '/windows')), denv);
check(r.status === 403, 'a link for one file does not open another');
r = await worker.fetch(new Request('https://jamesmireku.com' + out.url.replace(/s=[^&]+/, 's=forged')), denv);
check(r.status === 403, 'a forged link is refused');
check(!(await checkTicket('test-secret-for-downloads', 'library', String(Date.now() - 1000), 'x', Date.now())), 'an expired link is refused');
const old = await signTicket('test-secret-for-downloads', 'library', Date.now() - 1000);
r = await worker.fetch(new Request('https://jamesmireku.com' + old), denv); check(r.status === 403, 'an expired link is refused by the server too');
owns = false; team = false;
r = await ask('library', jwtFor('22222222-2222-3333-4444-555555555555')); check(r.status === 403, 'someone who does not own MIREKU gets no link');
team = true;
r = await ask('windows', jwtFor('33333333-2222-3333-4444-555555555555')); check(r.status === 200, 'the team gets links for testing');
r = await ask('library'); check(r.status === 401, 'nobody signed in gets no link');
r = await ask('secrets', jwtFor('33333333-2222-3333-4444-555555555555')); check(r.status === 404, 'an unknown file name is refused');
r = await worker.fetch(new Request('https://jamesmireku.com/api/download', { method: 'POST', body: '{}' }), { ...denv, DOWNLOAD_SECRET: '' });
check(r.status === 503, 'without its secret it says downloads are not set up');

console.log(fails ? fails + ' FAILED' : 'ALL PASSED'); process.exitCode = fails ? 1 : 0;
