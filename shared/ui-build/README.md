# UI build for Top End and Low End

Both apps' `resources/ui/index.html` are generated from these parts. Edit the
parts, never the generated `index.html`.

    node build.js top     # writes apps/topend/resources/ui/index.html
    node build.js low     # writes apps/lowend/resources/ui/index.html

- `products.js` holds everything that is the instrument or the brand: tunings,
  the player's stem, amp knobs and switches, amp faceplates, cabs, Low End's
  palette and wording.
- `build.js` assembles the parts, applies the manual-tone copy table (`COPY`)
  to both apps, then the product's wording and colours. A copy or wording entry
  that no longer matches its source stops the build.
- `old-index.html` is the inherited page that the bridge, menus, Learn storage,
  click and test-gate sections are cut from.

Layout check (headless Chrome, a mocked engine replaying a captured state, nine
window sizes including iPad, every tab):

    node harness.js <outdir> top
    node harness.js <outdir> low

It reports anything outside the window, cut off inside a card or a stem tile,
or overlapping, and saves a screenshot per size and tab.

`port-lowend.js` carries Top End's shared engine and plug-in files to Low End
with the product renamed (amp, cab, pedals, pitch range, tuner strings and
factory presets stay Low End's own). Run it after changing a shared file in
Top End, then rebuild and test Low End.
