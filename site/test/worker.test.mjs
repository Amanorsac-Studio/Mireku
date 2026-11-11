// Tests for site/worker.js (MIREKU account emails). Run: node site/test/worker.test.mjs
// No network: Resend is imitated, and the Supabase signature is made with a throwaway secret.
import worker, { verifySignature, buildLetter, isOurs, hitRow } from '../worker.js';
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

console.log(fails ? fails + ' FAILED' : 'ALL PASSED'); process.exitCode = fails ? 1 : 0;
