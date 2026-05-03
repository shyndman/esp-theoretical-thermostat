# Message Screen Design

Message screens are short, vertically-stacked notifications rendered on the thermostat. They are authored as text from a Home Assistant text box and parsed into a fixed block vocabulary. The renderer is intentionally dumb: it composes blocks from top to bottom, picks an accent color when told, and decides where the stack sits on the screen. It does not know anything about time, dates, senders, or message intent — that logic lives in the HA template that produces the text.

The reference HTML mockup is at `assets/message-screens.html`. The .pen design is `assets/theoretical-thermostat.pen` (top-level frames `Message Screen - *`).

## Anatomy

Every screen is a chrome plus a body region.

- **Chrome** (top 90px): weather summary on the left, screen **title** in the middle, room temp on the right, then a 2px divider. Always present.
- **Body region** (y 92 → 1280, width 720 with 60px gutters): a vertical flex stack of blocks.

Screens are 800×1280, dark (`#000`).

## Block Vocabulary

The body is a stack of these blocks. Style is named — never a raw font size in the editor.

| Block      | Size  | Weight | Letter-spacing | Line-height | Default color    | Wraps |
|------------|-------|--------|----------------|-------------|------------------|-------|
| `eyebrow`  | 36px  | 600    | 0.18em (~6.5)  | default     | muted (`#a0a0a0`)| no    |
| `headline` | 88px  | 700    | -0.02em (~-2)  | 1.05        | white            | yes   |
| `time`     | 96px  | 700    | -0.02em (~-2)  | default     | orange (`#e1752e`)| no    |
| `time`/suffix | 48px | 500   | 0              | default     | muted            | no    |
| `body`     | 42px  | 500    | 0              | 1.25        | white            | yes   |
| `spacer`   | 14px  | —      | —              | —           | —                | —     |

Top-level gap between blocks: **36px**. A `spacer` adds an extra 14px on top of that gap (so total 36 + 14 + 36 = 86px between two items separated by a spacer).

Why these specifically: every size above is one a constrained device can ship comfortably. There is no `display` block (340px) — one-glyph-only sizes are too costly per screen variant.

### Accent palette

Only colors, never sizes.

- `orange` — `#e1752e` (default for `time`, used to highlight)
- `blue` — `#5393c9` (used for `eyebrow` to draw urgency, for status titles)
- `gray` — `#a0a0a0` (muted; default for `eyebrow`, `time` suffix, and de-emphasized items)
- `white` — `#ffffff` (default for `headline`, `body`)

Modifiers attach to blocks; they do not change size or weight.

## Title

The chrome title is content (one of: TODAY, REMINDER, NOTICE, FROM HILLARY, BEDTIME — but ultimately user-defined). It is not part of the body stack. It accepts the same accent palette.

**Title vs eyebrow** — they are different axes:
- **Title** classifies the message (sender / category / channel). Lives in the chrome.
- **Eyebrow** is a temporal lead-in for the body, semantically tied to the headline beneath it.

A REMINDER screen uses both: title "REMINDER" classifies the message; eyebrow "TOMORROW" frames the headline.

## Positioning

The body stack's vertical position is automatic.

- **Default**: stack sits **5% above** the vertical center of the body region. Slightly-high reads as intentional; dead-center reads as droopy.
- **Long content**: once content height exceeds ~60% of the body region, the stack top-anchors just below the chrome divider so it can't collide with the header.
- **Override**: a leading `^` line in the editor forces top-anchor regardless of length — used for lists like meetings where the visual rhythm starts at the top.

Horizontal alignment is always left. There is no per-block alignment knob.

## Editor syntax

Plain text. Each line is one block. Sigil prefix selects the block; absence of a sigil means body. Blank line = `spacer`. Trailing `:color` sets the accent.

| Sigil  | Block        |
|--------|--------------|
| `=`    | title (chrome, conventionally first line) |
| `>`    | eyebrow      |
| `#`    | headline     |
| `@`    | time (use ` \| ` to split into time + suffix) |
| (none) | body         |
| `^`    | force top-anchor (line by itself) |
| blank  | spacer (14px) |

Defaults: title gray, eyebrow gray, time orange, headline white, body white. Add `:orange` / `:blue` / `:gray` / `:white` only when overriding.

### Examples

**Meetings** — list, top-anchored, varied accents per item:

```
= TODAY

^
@ 9:30 AM
Team standup

@ 11:00 AM :blue
1:1 with Sarah

@ 2:00 PM :blue
Design review

@ 4:30 PM :gray
Pick up Henry from daycare :gray
```

**Reminder** — single event, headline + time with suffix:

```
= REMINDER :orange
> TOMORROW
# Henry's vet appointment
@ 10:30 AM | Dr. Patel
```

**Alert** — brief, urgent:

```
= NOTICE :blue
> JUST NOW :blue
# Package on the porch
```

**Note from Hillary** — prose message:

```
= FROM HILLARY
> TONIGHT :blue
# Home late.
Leftover pasta in the fridge — feed Henry by 6. :gray
```

**Bedtime** — multi-body checklist with deadline:

```
= BEDTIME
> BEFORE BED
Lock the back door.
Start the dishwasher.
Set the alarm.
@ 10:30 PM :gray
```

## Where logic lives

The renderer paints what it is told. Anything that requires *knowledge* — picking blue for upcoming meetings and gray for past ones, choosing the right title for a sender, formatting a relative date as "TOMORROW" — is the responsibility of the HA template that emits the text. This keeps the on-device renderer small and deterministic, and lets message styling evolve without firmware changes.
