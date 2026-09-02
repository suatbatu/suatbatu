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

## 1b. Drain the task queue — do this before reading tasks.md for priorities

The dashboard's task box can't edit the repo directly, so it queues each edit by
mailing it to Batuhan with a `[dash]` subject prefix. Apply the queue first, so the
board you build reflects what he asked for.

`search_threads` with `in:inbox subject:"[dash]" newer_than:7d`. Each body is:

```
op: add | done | remove
task: <text>
priority: 1|2|3      (add only)
due: YYYY-MM-DD      (add only, may be blank)
tags: #tag           (add only, may be blank)
```

- **add** → append under `## Open` as `- [ ] <task>  !<pri>  @<due>  <tags>`,
  omitting `!`/`@`/tags when blank.
- **done** → flip that line's `- [ ]` to `- [x]`.
- **remove** → delete the line.

Match `done`/`remove` on the task text. **If nothing matches, do not guess and do not
trash the mail** — leave it in place and say so in the briefing, so a queued edit is
never silently swallowed. After successfully applying one, `trash_thread` its id so
tomorrow doesn't reapply it. Commit the tasks.md change with the rest of the run.

## 2. Pull the data

**Calendar** — `list_events` once per calendar in `config.yml`, from 00:00 today
to `horizon.schedule_days` ahead, `orderBy: startTime`, `timeZone: Europe/Istanbul`.

> **Query all four every single run. No exceptions, no "it was empty yesterday".**
> On 2 Sep 2026 a five-day trip to Taşos — booked since 9 August, sitting on the
> `Aile` calendar — was missed for a week because only the Personal and KOZALAK
> calendars were fetched on several mornings. The board spent that week telling
> Batuhan to diarise an event he could not attend. Secondary calendars are exactly
> where multi-day and all-day commitments live, and they are the ones that reframe
> a whole week. A calendar that returned nothing yesterday is not evidence about today.

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
  Give each one **`actions`** — see below. This is not optional; a ledger row with
  no way to act on it is just a reminder.
- **`priorities`** — from `tasks.md`, ranked by `!` then due date. Max ~8.
- **`counters`** — three numbers: events today, open items needing a reply,
  deadlines inside 7 days. Each gets a one-line `note`.
- **`briefing`** — 3–5 sentences of plain prose. Say what today actually demands,
  name the one thing most likely to go wrong, and be specific. Light `<em>` is
  allowed for event names. No greeting (the page writes its own), no filler,
  no "Have a great day!".

### Action links on the ledger

Every deadline gets one or two one-click buttons that land Batuhan exactly where the
thing gets resolved. Keep them to two — a third is clutter, not convenience.

```js
actions: [
  { label: "Statement", icon: "mail",  href: "https://mail.google.com/mail/u/0/#inbox/<threadId>" },
  { label: "Route",     icon: "map",   href: "https://www.google.com/maps/dir/?api=1&destination=..." }
]
```

- `icon` is one of `mail`, `reply`, `event`, `map`, `web`. Anything else falls back to `web`.
- **Gmail** — `https://mail.google.com/mail/u/0/#inbox/<threadId>`, using the thread
  `id` from `search_threads`. Append `?compose=new` for a "reply to cancel" style action.
- **Calendar** — use the event's own `htmlLink` field verbatim. Do not hand-build these.
- **Maps** — `https://www.google.com/maps/dir/?api=1&destination=<url-encoded place>`,
  only when the deadline genuinely involves travel.

**Only ever link to something that came from the data you actually fetched.** A thread
id you read, an `htmlLink` you were given, or a destination named in the event. Never
guess a bank's login URL, a government portal, or a company's account page from memory
— a wrong link on a payment deadline is worse than no link. If you have no real target,
give the row no `actions` at all.

Label buttons by what's on the other side ("Tahakkuk fişi", "Answer the invite"), not
by the mechanism ("Open Gmail").

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
