# Console screenshot and video reference audit

Reviewed 2026-11-17. These are real console captures and manufacturer walkthroughs, not AI mockups.
The linked media was inspected locally as stills and short frame sequences. Downloads and extracted
frames are temporary research material; they are not assets included in USCL. The screenshots under
`docs/images` instead show USCL components with synthetic titles and original USCL icons.

## Sources inspected

| Target | Online reference | Frames/areas inspected | Applied findings |
|---|---|---|---|
| PS4 | [Sony home/function captures](https://manuals.playstation.net/document/en/ps4/basic/screens.html), [Sony 2016 Quick Menu article and screenshot](https://blog.playstation.com/archive/2016/09/14/how-the-new-ps4-quick-menu-places-everything-you-need-at-the-touch-of-a-button) | Home, functions, and the linked Quick Menu image | Separate three-area navigation; larger quick-menu heading, compact rows, outlined focus and profile footer; function icons travel while the profile fades |
| PS5 | [PlayStation's 2020 UI walkthrough](https://www.youtube.com/watch?v=7TBPrYJDoDE), [publisher context](https://blog.playstation.com/2020/10/15/first-look-playstation-5s-next-generation-user-experience/) | 8:30 game home, 9:00/10:00 strip, 10:20 Control Center; 8:31–8:34 frame sequence | Strip close to the top, enlarged selected cover, caption beside it, smaller top utility glyphs, Play plus Options, cards above compact bottom controls |
| Switch | [Nintendo's 2017 HOME walkthrough](https://www.youtube.com/watch?v=B8g2-ZtbjXk) | 0:20 HOME, software and six circular shortcuts | Square covers, mint selection, title above software, larger separate system circles and the earlier six-item arrangement |
| Switch 2 | [Nintendo HOME walkthrough](https://www.youtube.com/watch?v=MFUh-KhOhMs), embedded in the [official welcome center](https://www.nintendo.com/us/gaming-systems/switch-2/welcome-center/); [support overview](https://en-americas-support.nintendo.com/app/answers/detail/a_id/68181) | 0:10–0:14 HOME → software library, 0:20 library; 1:05 article focus | Rounded covers, ten compact grouped tools, red online circle, multicolor focus outlines, blue labels, five-column library |
| Xbox One | [Xbox's original 2013 demo](https://www.youtube.com/watch?v=xhe6jV-APwM) | 0:45 direct Home feed; 1:55 Pins | Black background, wide hero, stacked system tiles, four bottom tiles, separate news rail; Pins has three rows of square tiles instead of reusing Home geometry |
| Xbox 360 2012 | [IGN's direct dashboard walkthrough](https://www.youtube.com/watch?v=3zOX7_67dIQ), [Xbox's release announcement](https://news.xbox.com/en-us/2012/10/15/new-xbox-360-dashboard-update-starts-rolling-out-today/) | 0:20 dashboard frame | Confirms existing nine-tile geometry; new directional pivot entrances preserve that layout |

The PS3 crossbar and Xbox 360 Classic geometry retain the prior local retail-reference work. This pass
adds Classic blade motion, rather than claiming a new online, frame-exact measurement of that animation.
[Sony's PS3 XMB guide](https://manuals.playstation.net/document/en/ps3/3_10/basicoperations/xmb.html) is an
additional public navigation reference. Later firmware changes are not mixed into the earlier Switch,
PS5 launch or Xbox 2013 targets. Sony labels its PS5 reveal as a preproduction walkthrough.

## Measured geometry and implementation targets

Coordinates below refer to the 1920×1080 USCL stage. The inspected direct-feed videos are 1280×720, so
positions and sizes are scaled by 1.5. Measurements from compressed frames are approximate, not a
claim that the app matches every reference pixel.

| Detail | Approximate reference | USCL target |
|---|---|---|
| PS5 selected software origin | (168, 123) | (168, 123) |
| PS5 selected / neighboring cover | 177 / 105 square | 180 / 108 square |
| PS5 Control Center selected card | (78, 366), roughly 443×530 | (84, 366), 438×528; USCL cards use Library, Updates and News |
| PS5 bottom control centers | roughly y=1011 | y=1007, spread across the lower center; eight USCL controls |
| PS5 Play origin | (171, 843) | (171, 843) |
| Switch 2 first cover origin and size | (159, 300), 384 square | (159, 300), 384 square |
| Switch first cover origin and size | (159, 291), 384 square | (159, 291), 384 square |
| Switch 2 first toolbar center / pitch | x≈402 / 123 | x=400 / 124 |
| Xbox One Home hero | roughly 870×438 in the demo feed | 864×432, with six-pixel gutters |
| Xbox One Pins | three rows of square tiles | 210 square, 216 pitch, fifteen positions per page |

## Motion and functional substitutions

Frame sequences were used to check ordering, focus treatments and whether movement is a slide or a fade.
They were not used to claim exact easing curves or input-to-display latency: edited videos, compression,
unknown controller timing and frame sampling limit those conclusions. The previously measured PS4 shelf
uses 150 ms ease-out and its delayed title fade; new Xbox entrances use a restrained 200 ms slide and
160 ms fade. Quick-menu open/close remains reversible at 250 ms. These are implementation timings.

All cards and tiles open existing USCL functions. PS5 cards provide Library, Game Updates and USCL News;
they do not invent platform activities or trophy progress. Xbox's Store position becomes Library and its
news rail offers USCL News, Controllers and Sound. Nintendo system positions map to the documented USCL
shortcuts. Original fonts, icons, artwork and these functional substitutions still differ from console OSs.

Regression checks cover the measured home anchors, interrupted pivots, PS5 card/control navigation and
Options without accidental launch, Xbox pin-page boundaries, opaque rounded cover corners, and full
keyboard/mouse access. They do not certify perceptually exact motion or physical-controller latency.

## Follow-up accuracy pass

The 10:20 Sony frame showed that the prior USCL Control Center cards were undersized and too low.
The revised cards use the measured portrait proportions, a small header glyph, a preview region, muted
context and a stronger title. The library preview uses up to four live game icons and the actual game
count, with an empty-library fallback; no reference artwork is bundled. Bottom controls are spaced
across the lower center with labels remaining inside the stage. The eight available USCL controls
substitute for Sony's larger service-specific set.

Nintendo's HOME footage shows long software names travelling within a clipped area above the selected
cover. Both Nintendo themes now use a dedicated bounded title region: short titles stay still, long
titles pause before scrolling to their ending, and selection changes reset their position. Travel
stops while hidden, while quick access is open and when motion is disabled. The 1200 ms initial pause,
60 stage-pixels/second travel and 1000 ms ending pause are conservative implementation choices, not
frame-exact measurements. Regression tests check timing order, reset/visibility behavior, card bounds
and empty-library actions on software and OpenGL renderers.

## Library sorting references

[Nintendo's Switch 2 sorting instructions](https://en-americas-support.nintendo.com/app/answers/detail/a_id/68326)
show separate filter and reorder controls, including Title, and state that library sorting leaves HOME
icons unchanged. [Sony's library support guide](https://www.playstation.com/en-sg/support/games/hide-games-playstation-library/)
identifies a Filter and Sort control. USCL now provides a working sort chooser across modern libraries.
Its title and console orders use available USCL data; recent play, play time and publisher orders are
not shown because those fields are not tracked. This improves the workflow without claiming identical
platform services, metadata or library geometry.
