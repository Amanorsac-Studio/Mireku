# MIREKU website

The public site for MIREKU: a landing page, a buy button, and an account
page with the licence key, the computers using it, and the downloads.

Static files on a Cloudflare Worker (no Worker script). Accounts, purchases,
licence keys and device activations are the studio's existing Supabase
project; payment is the studio's existing `create-app-paystack` function;
downloads are signed by amanorsac.studio's `/api/app-download`. Nothing on the
studio side had to change for this site to work.

```
site/
  wrangler.jsonc          Cloudflare settings. "name" must match the Worker.
  public/                everything that is served
    index.html           landing page
    account.html         sign in, licence key, computers, downloads
    mireku.html          where Paystack returns a buyer; forwards to /account
    assets/config.js     SELLING switch and the publishable Supabase key
    _headers, robots.txt security headers; noindex until launch
  studio/
    catalog-entry.json   the entry to add to amanorsac.studio's catalog.json
```

## Why nothing can be sold yet

Two separate switches must both be on before the button takes money:

1. `SELLING: true` in `public/assets/config.js` (this repository).
2. The `mireku` entry in amanorsac.studio's `catalog.json` with
   `"status": "available"` and a price (the studio's repository).

Until both are on, every buy button reads "Coming soon". The server checks the
catalog again before charging, so the page cannot be talked into selling.

The site also tells search engines to stay away (`X-Robots-Tag: noindex` in
`_headers`, `Disallow: /` in `robots.txt`) until launch day.

## Keys

The only key in this folder is Supabase's **publishable** key, the same one
amanorsac.studio already serves to every visitor. The service key, the
Paystack secret key and any Resend key stay in Supabase's function secrets and
Cloudflare's secrets. Never put them in a file here.

## How a sale flows

1. Visitor presses Buy. Not signed in: sent to `/account?buy=1`, then back
   into the checkout once signed in.
2. The page calls `create-app-paystack` with `{ app: "mireku" }`. The function
   reads the price from amanorsac.studio/catalog.json and opens Paystack.
3. Paystack returns the buyer to `<this site>/mireku?purchased=1`
   (the function always returns to `/<app id>`, and already accepts
   workers.dev addresses). That page forwards to `/account?purchased=1`.
4. `paystack-webhook` records the purchase; the database mints the key
   (`MIRE-XXXX-XXXX-XXXX`). The account page waits for it and shows it.

## Launch checklist (one at a time)

The site's address is **https://jamesmireku.com** (custom domain on the
`mireku` Worker; the workers.dev addresses are switched off).

1. Cloudflare Worker name is `mireku`, matching `wrangler.jsonc`. Done.
2. Supabase → Authentication → URL Configuration → Redirect URLs: add
   `https://jamesmireku.com/**`.
3. Merge this branch into `main`. Cloudflare builds and serves the site,
   still closed to search engines and with selling off. (Builds from `main`
   fail until then, because `main` has no `site` folder.)
4. Studio repository, `supabase/functions/create-app-paystack/index.ts`:
   `returnOrigin` must accept `https://jamesmireku.com`, or Paystack sends
   MIREKU buyers back to amanorsac.studio. Add
   `if (o === "https://jamesmireku.com") return o;` beside the SITE check and
   redeploy the function.
5. Studio repository: add `studio/catalog-entry.json`'s `mireku` entry to
   `catalog.json` (status `hidden`, so it shows nowhere on amanorsac.studio).
   Copy `public/images/icon.png` and `public/images/01-perform-800.webp` to
   `images/apps/mireku/`.
6. Upload the installers to the `amanorsac-downloads` R2 bucket and fill
   `installers` (`windows`, `mac`), as for the other apps.
7. Price: set `price_cents` (dollars × 100). For a round cedi figure, also set
   `paystack_price` in pesewas (cedis × 100).
8. Emails: the separate Resend account, verified for jamesmireku.com.
9. Go-ahead: set the catalog `status` to `available`, `SELLING: true` here,
   remove the noindex line and the robots Disallow.

## Open decisions

- **James's share.** Paystack can split a payment to a subaccount, but
  `create-app-paystack` does not send one today. Either pay James from the
  Paystack reports by hand, or add a `subaccount` field to the catalog entry
  and pass it through in that function (a change in the studio repository).
- **Shelf tile on amanorsac.studio.** Tiles are written by hand in `apps.html`.
  The catalog entry above is `hidden`, so nothing appears until a tile is added
  and the status changes.
- **$1 trial.** Every licensed paid app gets the studio trial unless its entry
  says `"trial": false`. The entry above opts out; delete that line to offer it.
- **Library download (5.4 GB).** Installers are keyed by platform only. The
  library needs its own delivery decision before launch.
- **Emails.** Sign-up and reset emails come from the studio's hook with
  Amanorsac branding until the separate Resend account is set up for
  jamesmireku.com (step 8).
- **"Nothing leaves your computer."** The marketing notes say MIREKU makes no
  network request. Once the app checks keys with the studio, that line is no
  longer true and must not be used. The site does not use it.
- **James's biography and photos** need his sign-off. The site uses only the
  portrait already in the app.

## Third-party files

- Barlow Semi Condensed and Inter fonts: SIL Open Font License 1.1.
- `assets/vendor/supabase-2.116.0.js`: supabase-js, MIT licence (the same
  build amanorsac.studio serves).

## MIREKU's own account emails

Sign-up, password-reset and sign-in emails for people who started on
jamesmireku.com are sent by `worker.js` from `hello@jamesmireku.com`, with the
jamesmireku.com Resend account. Supabase hands every account email to
amanorsac.studio; `studio/potfoliowebsite-mireku-emails.patch` makes that code
pass MIREKU's on to `https://jamesmireku.com/api/auth-email`, still carrying
Supabase's signature, which `worker.js` checks before sending anything. If the
MIREKU email fails, amanorsac.studio sends its own instead.

Secrets on the `mireku` Worker (never in a file): `RESEND_API_KEY` (Resend,
sending access, jamesmireku.com only) and `AUTH_HOOK_SECRET` (the same value
as Supabase → Authentication → Hooks → Send Email hook secret).

Tests: `node site/test/worker.test.mjs`.
