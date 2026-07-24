# MooBot Character — Sprite Generation Prompts (Cow / SKU-B)

Master prompts for generating expression sheets with Nano Banana / Gemini.
Workflow, format rules and quality gate at the bottom.

## Sheet format (locked)

- One sheet per expression: **3×3 grid, 9 frames**, read left→right, top→bottom
- Square canvas, **1024×1024 minimum**, PNG
- **Pure black background (#000000)** — matches device screen bg
- **Fur patch dark charcoal (#2B2B30)**, never pure black (would vanish on device)
- Closed-eye lashes drawn **white where they cross the patch**
- Face identical size + position in every cell; only described features change
- No grid lines, no labels, no numbers, no watermark

Save location: `firmware/assets/cow/raw/` — filenames EXACTLY as listed
(converter parses `cow_NN_<name>.png`).

Convert: `python3 tools/sprites2lvgl.py --persona cow`

---

## Step 1 — Character reference (run once, reuse forever)

```
Create a single character reference image: a cute baby cow mascot face for a desktop companion robot, front-facing, perfectly symmetrical, centered.

Style: flat kawaii vector style, thick rounded shapes, clean edges, no outlines thinner than 4px, no gradients, no 3D shading, limited palette (white, black, soft pink, warm gray).

Features: large glossy black eyes with two white highlight dots each, small pink muzzle with tiny oval nostrils, soft pink blush circles on cheeks, two tiny rounded gray horns on top, small floppy ears, one dark charcoal gray (#2B2B30) Holstein patch over the left eye area.

Composition: face only, no body, face fills about 75% of canvas, pure black background (#000000), no text, no watermark, no border.
```

Saved as: `firmware/assets/cow/raw/cow_ref.png`

## Step 2 — Expression sheets

Attach the reference image (or stay in the same chat session) for EVERY sheet.

### 01 — `cow_01_idle_blink.png` ✅ generated

```
Using the attached character reference EXACTLY — same face, same proportions, same colors, same line thickness, same scale — create an animation sprite sheet.

Layout: one square image, 3x3 grid, 9 equal cells, frames read left-to-right then top-to-bottom. Pure black background (#000000) everywhere, fur patch in dark charcoal gray (#2B2B30). NO grid lines, NO cell borders, NO frame numbers, NO labels, NO text, NO watermark. The face stays centered at identical size and position in every cell; ONLY the features described below change between frames.

ANIMATION — "idle blink": Frames 1-3: eyes fully open, calm tiny smile. Frame 4: soft sleepy half-blink with gentle curved lashes. Frame 5-6: eyes fully closed, cute curved lash line, drawn white where it crosses the fur patch. Frames 7-9: eyes fully open again, identical to frame 1.
```

### 02 — `cow_02_idle_look.png`

```
Using the same character and exact same style as the previous sprite sheets, create the next animation sprite sheet.

Layout identical to before: one square image, 3x3 grid, 9 equal cells, frames read left-to-right then top-to-bottom, solid pure black (#000000) background in every cell, fur patch in dark charcoal gray (#2B2B30), NO grid lines, NO labels, NO text. Face centered at identical size and position in every cell.

ANIMATION — "idle glance": Eyes open the whole time, calm tiny smile. Frames 1-2: pupils centered. Frames 3-4: both pupils glance to the left. Frame 5: pupils back to center. Frames 6-7: both pupils glance to the right. Frames 8-9: pupils centered again, identical to frame 1. Only the pupils move; eyelids, face and everything else stay still.
```

### 03 — `cow_03_pat.png`

```
Using the same character and exact same style as the previous sprite sheets, create the next animation sprite sheet.

Layout identical to before: one square image, 3x3 grid, 9 equal cells, frames read left-to-right then top-to-bottom, solid pure black (#000000) background in every cell, the fur patch in dark charcoal gray (#2B2B30), NO grid lines, NO labels, NO text. Face stays centered at identical size and position in every cell.

ANIMATION — "head pat happiness": Frames 1-2: eyes open, normal smile. Frames 3-7: eyes closed into happy upward arcs (^ ^) with the white lash line where they cross the fur patch, smile grows wide, blush circles get bigger, two small pink hearts float upward beside the head, rising and growing a little each frame. Frames 8-9: hearts shrink and fade, eyes begin reopening, big content smile stays.
```

### 04 — `cow_04_happy.png`

```
Same character, same style, same layout as before (3x3 grid, 9 cells, black #000000 background, charcoal #2B2B30 patch, no labels, face identical size and position every cell).

ANIMATION — "happy": Frames 1-4: mouth opens progressively into a big joyful smile, eyes become upward curved arcs. Frames 5-7: two or three tiny white sparkles appear near the cheeks. Frames 8-9: sparkles fade, big smile and arc eyes remain.
```

### 05 — `cow_05_celebrate.png`

```
Same character, same style, same layout as before (3x3 grid, 9 cells, black #000000 background, charcoal #2B2B30 patch, no labels, face identical size and position every cell).

ANIMATION — "celebrate": Huge open smile the whole time, eyes sparkling with star-shaped highlights. Frames 1-6: tiny colorful confetti dots and small stars pop into place around the head, more each frame. Frames 7-9: confetti gently settles and fades, proud happy face remains.
```

### 06 — `cow_06_sad.png`

```
Same character, same style, same layout as before (3x3 grid, 9 cells, black #000000 background, charcoal #2B2B30 patch, no labels, face identical size and position every cell).

ANIMATION — "sad": Frames 1-3: ears droop downward gradually, eyes lower and lose their sparkle. Frames 4-6: mouth turns into a small frown, eyes begin to glisten. Frames 7-9: a single small tear drop forms at the corner of the left eye and grows slightly. Keep it gentle and cute, not dramatic.
```

### 07 — `cow_07_sleepy.png`

```
Same character, same style, same layout as before (3x3 grid, 9 cells, black #000000 background, charcoal #2B2B30 patch, no labels, face identical size and position every cell).

ANIMATION — "sleepy": Frames 1-5: eyelids get heavier each frame, head tilts very slightly to one side. Frames 6-9: eyes fully closed with the same curved lash style as the blink sheet (white lash line over the patch), a small white "z" floats up at the top right in frame 6, becoming "zZ" and rising a little each following frame.
```

### 08 — `cow_08_alert.png`

```
Same character, same style, same layout as before (3x3 grid, 9 cells, black #000000 background, charcoal #2B2B30 patch, no labels, face identical size and position every cell).

ANIMATION — "alert reminder": Frames 1-2: neutral calm face. Frame 3: eyes snap wide open, ears perk upward. Frames 4-7: a small golden bell appears beside the head at the right, tilting left then right on alternating frames as if ringing, with two tiny motion lines. Frames 8-9: bell fades out, eyes stay wide and attentive.
```

### 09 — `cow_09_task_done.png`

```
Same character, same style, same layout as before (3x3 grid, 9 cells, black #000000 background, charcoal #2B2B30 patch, no labels, face identical size and position every cell).

ANIMATION — "task done nod": Confident happy look. Frames 2-4: the whole face tilts downward slightly as a nod. Frames 5-6: face comes back up to center. Frames 6-9: a green checkmark pops in beside the head on the right, growing to full size, with a proud smile on the face.
```

### 10 — `cow_10_wake.png`

```
Same character, same style, same layout as before (3x3 grid, 9 cells, black #000000 background, charcoal #2B2B30 patch, no labels, face identical size and position every cell).

ANIMATION — "wake up": Frames 1-2: eyes fully closed, peaceful. Frames 3-5: squinting awake, left eye opens first, then right. Frames 6-9: both eyes bright and open, fresh small smile, tiny warm sun rays hinted at the two top corners fading in gently.
```

### 11 — `cow_11_eat.png`

```
Same character, same style, same layout as before (3x3 grid, 9 cells, black #000000 background, charcoal #2B2B30 patch, no labels, face identical size and position every cell).

ANIMATION — "eating grass": Frame 1: a green grass strand hangs from the mouth, content expression. Frames 2-8: the muzzle does a subtle chewing motion, shifting slightly up, down and sideways in alternation, the grass strand getting shorter each frame. Frame 9: grass gone, satisfied happy expression with closed content eyes.
```

---

## Quality gate (check every sheet before saving)

1. Background pure black edge-to-edge — no white, no dark-gray gradient
2. Patch charcoal, visibly lighter than background
3. Face same size and position in all 9 cells
4. Closed-eye lashes visible over the patch (white line)
5. No stray text, numbers, grid lines, watermarks
6. Frame progression reads correctly left→right, top→bottom

Fail any → regenerate before saving. Fix with an edit prompt in the same chat:
attach the bad sheet, describe only what to change, keep everything else.

## Firmware mapping

Filename `<name>` → `moo_anim_<name>` symbol + `MOO_HAS_ANIM_<NAME>` define
(generated in `firmware/components/persona_assets/cow_anims.h`). Missing
sheets are fine — firmware falls back gracefully until they exist.

FPS / loop behavior per animation lives in `ANIM_PARAMS` inside
`tools/sprites2lvgl.py`.

## Robot persona (SKU-A) — later

Same pipeline, persona `robot`: new character reference, same 11 expression
specs (bell → antenna ping, grass → energy bolt, etc.), files
`robot_NN_<name>.png`, run converter with `--persona robot`.
