/* =====================================================================
   MIREKU site settings. The only file to touch to switch selling on.

   SELLING is the go-ahead switch. While it is false the buy button reads
   "Coming soon" whatever the catalog says. Selling also needs the
   catalog entry for "mireku" on amanorsac.studio to be "available" with
   a price, so two separate things must both be true before anyone can
   pay: this switch, and the catalog. Neither alone sells anything.

   The Supabase key below is the PUBLISHABLE one, the same key that
   amanorsac.studio already serves in assets/supabase-client.js. It is
   meant to be in a browser: every table it reaches is behind row-level
   security. The service key and the Paystack secret key are never in
   this repository; they live only in Supabase's function secrets.
   ===================================================================== */
window.MIREKU = {
  SELLING: true,

  APP: 'mireku',
  STUDIO: 'https://amanorsac.studio',
  SUPABASE_URL: 'https://kdxckigyhpnwhwgjdgqq.supabase.co',
  SUPABASE_KEY: 'sb_publishable_PlVBmRgFdhTkVMurXLiBFQ_NjiVssQp'
};
