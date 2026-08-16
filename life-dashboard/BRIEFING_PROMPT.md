# Morning briefing — agent instructions

You are rebuilding Batuhan's life dashboard for today. A Routine fires this every
morning at **07:55 Europe/Istanbul** in a fresh session with no memory of yesterday,
so everything you need is here or in the repo.

Work quietly. Do not message the user, do not ask questions, do not open a pull
request. The deliverable is a republished page and a pushed commit.

---

## 0. Get on the right branch

```bash
cd /home/user/suatbatu 2>/dev/null || cd "$(git rev-parse --show-toplevel)"
git fetch origin claude/life-dashboard-7btigi && git checkout claude/life-dashboard-7btigi && git pull origin claude/life-dashboard-7btigi
```

If that branch no longer exists, it was merged — use the default branch instead
and continue. If `life-dashboard/` is missing on both, stop and do nothing.

## 1. Read the inputs

- `life-dashboard/config.yml` — calendar IDs, inbox query, mute/priority senders,
  horizons. **This file wins over anything written below.** If Batuhan edited it,
  follow the edit.
- `life-dashboard/tasks.md` — the task list. Parse `- [ ]`, `!1..!3`, `@YYYY-MM-DD`, `#tag`.
  Ignore `- [x]` items and everything under `## Someday`.

## 2. Pull the data

**Calendar** — `list_events` once per calendar in `config.yml`, from 00:00 today
to `horizon.schedule_days` ahead, `orderBy: startTime`, `timeZone: Europe/Istanbul`.

- Normalize every time to Istanbul. Some events carry a foreign `timeZone` label
  (Europe/Paris, Europe/Lisbon, Europe/Moscow) — **trust the numeric UTC offset in
  the timestamp, not the label.**
- Note each event's own `responseStatus` for Batuhan (`needsAction` = unanswered).
- Detect overlaps within a day and mark both events `conflict: true` with a short
  `conflictNote` saying what the collision is.

**Gmail** — `search_threads` with `inbox.query` and `inbox.max_threads`.

- Drop anything from `mute_senders`.
- Always surface anything from `priority_senders`.
- Classify each shown thread: `action` (needs a reply or a decision),
  `watch` (informational but live, e.g. a failing CI run), `done` (resolved, FYI).

## 3. Decide what matters

Build the day's judgment, not just a dump:

- **`needsYou`** — things blocking on Batuhan specifically: unanswered invites,
  security actions, filings, payments. Give each a real `why` that says what
  happens if it's ignored. Severity `critical` or `warn`.
- **`deadlines`** — anything dated inside `horizon.deadline_days`, from tasks,
  calendar all-day markers, and mail. Sorted by date; the page computes countdowns.
- **`priorities`** — from `tasks.md`, ranked by `!` then due date. Max ~8.
- **`counters`** — three numbers: events today, open items needing a reply,
  deadlines inside 7 days. Each gets a one-line `note`.
- **`briefing`** — 3–5 sentences of plain prose. Say what today actually demands,
  name the one thing most likely to go wrong, and be specific. Light `<em>` is
  allowed for event names. No greeting (the page writes its own), no filler,
  no "Have a great day!".

**Honesty rules.** Never invent a date, an amount, or a deadline. If you're
inferring a due date from convention rather than reading it (e.g. Turkish tax
deadlines), set `unverified: true` on that deadline so the page renders a
"confirm date" chip. If a data source fails, say so in `briefing` and carry on
with the rest — a partial board beats no board.

## 4. Rewrite the page

Edit `life-dashboard/dashboard.html`. **Replace only the block between**

```
// ===================== DATA START =====================
// ====================== DATA END ======================
```

Do not touch the CSS, the markup, or the render script — the layout is done. Keep
every field name exactly as it is; the renderer reads them by name. `days[0]` must
be today (use `emptyNote` when today is clear), `days[1]` tomorrow.

Sanity-check before publishing:

```bash
node -e 'const fs=require("fs");const h=fs.readFileSync("life-dashboard/dashboard.html","utf8");
const b=[...h.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m=>m[1]);
const s={};new Function("g","with(g){"+b[0]+"; g.DATA=DATA;}")(s);
console.log("OK",s.DATA.today.date,s.DATA.days.length,"days",s.DATA.deadlines.length,"deadlines");'
```

## 5. Publish to the same URL

```
Artifact({
  file_path: "/home/user/suatbatu/life-dashboard/dashboard.html",
  url: "https://claude.ai/code/artifact/aa6b6dec-103f-413c-8a5e-2b2a0bd817b8",
  favicon: "🧭",
  label: "<today's date>"
})
```

The `url` is what keeps the link stable — **never publish without it**, or Batuhan
gets a new link every morning and his bookmark goes stale.

## 6. Archive and push

Write `life-dashboard/briefings/YYYY-MM-DD.md` — same content as the page, in
markdown, following the shape of the existing files in that folder. This archive
is what a future weekly-review agent reads, so keep it complete rather than terse.

```bash
git add life-dashboard/
git commit -m "Morning briefing YYYY-MM-DD"
git push -u origin claude/life-dashboard-7btigi
```

Retry a failed push up to 4 times with 2s/4s/8s/16s backoff.

---

## Changing any of this

Batuhan edits `config.yml` for *what* gets read, `tasks.md` for *what he owes*,
and this file for *how the briefing thinks*. All three take effect the next
morning with no other setup.
