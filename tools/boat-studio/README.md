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

## Drive-test scene prototype (work in progress)

`drive-test-scenes.html` preserves the owner's exported interactive **Køretestens scener**
preview (three scene groups, six selectable examples per group). Open this file
locally in a browser to review, compare, and revise the **existing** illustrations,
mooring lines, fenders, water effects, and animations. The export-only outer HTML
wrapper was removed; the inner interactive design code was kept unchanged.

**Not an approved final design and not embedded in firmware.** The owner is
not satisfied with the current options and wants to improve this exact preview,
not start over. Keep the earlier approved boat shape in `studio.html`/
`line-style.html` as the reference when revising the scenes. Do not replace
`firmware/ui/app.html` or publish firmware until the owner chooses the final look.
