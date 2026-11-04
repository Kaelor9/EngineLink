# Boat studio

`studio.html` is the page the owner used to build the boat for the drive test
scenes: a sport cruiser seen from above, bow up, drawn in SVG from code
(`boatSvg()`), with a choice for every part. Open it in a browser.

`chosen.png` is the owner's boat. Their choice, as the page's "Kopiér valget"
code:

```
Klamper: antal 1 · Klamper: placering 1 · Klamper: type 1 · Klamper: retning 1 · Stævn og anker 6 · Beslag og gelænder: metal 1 · Gelænder 2 · Badestige 8 · Badestige: side 1 · Fendere: antal 3 · Fendere: side 1 · Fendere: hængemåde 1 · Fendere: type 1 · Fendere: farve 1 · Fendere: bundet 1 · Fenderline: farve 1 · Form 5 · Rude 6 · Tag 11 · Cockpit 7 · Fordæk 6 · Badeplatform 1 · Badeplatform: kant 5 · Tagfarve 2 · Kaleche og stof 3 · Ruderamme 1 · Glas 1 · Skrogside 3 · Skrogstribe 1 · Dæk 1 · Cockpitgulv 2 · Hynder 3 · Stil 2
```

In words: short and broad hull with navy sides and white deck, hull carried
aft either side of a teak platform (one hatch), tall windscreen with long
wings, long light grey hardtop with two glass hatches, open cockpit with table
and teak floor, square hatches on the foredeck, bow fitting without anchor,
rail from the windscreen forward, four classic cleats (two aft, two up by the
bow), swim ladder stowed under a hatch on the starboard side of the platform,
folded out with the lid shut, three white cylinder fenders a side hanging
upright and tied to the rail with white line, fine lines style.

`line-style.html` is the same boat in the app's line style (flat colours, a
few strong lines), chosen for the drive test scenes, with four colour sets;
"Som appen" is the one that matches the app.

## Drive test scenes (chosen 2026-11-04)

`drive-test-scenes.html` is the options page for the drive test scenes. It has:
- a live preview;
- choices for colours, the boat's motion, the lines (16), the fenders (21), the
  order of fenders and lines, the figure-of-eight on the cleats (look and
  reveal), the waves (20) and their motion (11);
- a speed for each animated part;
- the quay layout for each of the three scenes.

Open it in a browser. "Kopiér valget" prints the choice.

The owner's choice is in `export_scenes.js`. To put a new choice into the app:
1. Change `CHOICE` in `export_scenes.js`.
2. Run `node tools/boat-studio/export_scenes.js` (needs Playwright).
3. Run `python3 tools/build_ui.py`.

## Owner's wishes, still to cover

Earlier wishes for the drive test scenes (from HANDOVER, 2026-11-03; the
2026-11-04 choice above supersedes the look where they differ):

- A boat with swim platform and cleats (also one on the bow); lines tied to
  those cleats. Choices for hull shape, colour, foredeck, cockpit, lounge,
  platform and the cleats (count, placement, direction, look).
- Open water without the dashed circle and the wake triangle; waves that fade
  in and glide out, never popping up.
- At the dock: two ways of mooring in turn, stern-to between piles (piles just
  outside the boat's beam, more empty berths with piles beside it) and
  alongside with bow line, stern line and two springs. Fenders out first, with
  a hint where they belong (where the hull is widest, towards the quay or the
  neighbours), then the lines one by one.
- Lines that end properly: figure-of-eight on cleats, a bowline (pælestik)
  around piles, an eye over a bollard.
- Fenders: his are standard ones (dark blue body, black tapered ends, an eye
  at each end) hanging vertically from one end. Choice of count per side and
  placement, always kept where the hull is near full beam.
- Choice of quay (concrete, wooden jetty, stone, rubber strip, steel sheet
  piling) and of line colour and type.
- Safety steps one at a time with "Done, next" (already in 0.14.1), dock order:
  nobody in the water, moored with fenders out, how to stop, engines at idle.

His choice on the earlier (old-style) page: Form 5 (wide) · Colour 1 (white) · Foredeck 6
(sunpad and rail) · Cockpit 2 (windscreen and helm bench) · Lounge 1 (empty) ·
Platform 5 (teak with ladder) · 4 cleats · at the rail · along the boat ·
cleat look 4 (polished steel) · Water A (drifting swells) · Fender 19 (navy
ball, but his real ones are the standard type, hanging from one end) · 3 per
side · evenly spread · line colour 6 (red) · braided · quay point 7 (cleat on
a plate) · pile 6 (with shadow) · quay 2 (wooden jetty). He still finds it too
minimal; the next step offered is a slanted (three-quarter) perspective, real
daylight colours and small movements (boat rocking, lines tightening).
