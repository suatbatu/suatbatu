# Life Dashboard

One page that pulls your calendars, inbox and task list together, rebuilt every
morning at **07:55 Istanbul time** by an agent that runs whether or not you're there.

**→ [Open the board](https://claude.ai/code/artifact/aa6b6dec-103f-413c-8a5e-2b2a0bd817b8)**
— private to your account, works on a phone, bookmark it. The link never changes.

---

## How it works

```
  07:55 Europe/Istanbul
        │
        ▼
  ┌───────────┐   fires a fresh session with Gmail + Calendar access
  │  Routine  │   (account-level — survives closing this chat)
  └─────┬─────┘
        │ reads BRIEFING_PROMPT.md
        ▼
  ┌──────────────────────────────────────────────┐
  │  Google Calendar ×4   Gmail    tasks.md      │
  └───────┬──────────────────┬─────────┬─────────┘
          └──────────────────┴─────────┘
                       │  finds conflicts, unanswered invites,
                       │  deadlines, and what's actually urgent
                       ▼
        ┌──────────────────────────────┐
        │  rewrites the DATA block in  │
        │  dashboard.html, republishes │──▶ same URL, updated in place
        │  writes briefings/<date>.md  │──▶ pushed to this branch
        └──────────────────────────────┘
```

## What's in here

| File | What it's for |
|---|---|
| `dashboard.html` | The page itself. All content lives in one `DATA` block at the top; the CSS and renderer below it never change. |
| `config.yml` | Which calendars, which inbox query, which senders are noise. **Edit this to change coverage.** |
| `tasks.md` | Your task list. **Edit this whenever** — GitHub's web editor works on a phone. |
| `BRIEFING_PROMPT.md` | The instructions the morning agent follows. Edit to change how it thinks. |
| `briefings/` | One markdown file per day. The searchable archive. |

## The two buttons at the top

**Refresh** re-reads Google Calendar and Gmail *from the page itself*, using your
credentials — no agent involved. The schedule, inbox and today's counter go live and
get an `updated 14:32` stamp. What it can't touch is the written briefing, Needs you,
the ledger and priorities: those are judgment plus `tasks.md`, not a connector call.
Once you refresh, the page says so explicitly rather than letting you assume it's all
current. If a connector is down, only that panel shows an error and the rest carries on.

**Full rebuild** opens the Claude session that runs your morning briefing, where
"rebuild my dashboard now" regenerates everything. There is deliberately no button
that does this by itself — no artifact capability can run an agent.

## Task box

Add, complete or delete a task without leaving the page. It works by mailing the
edit to yourself with a `[dash]` subject; the next rebuild applies it to `tasks.md`,
commits it, and trashes the mail. Added tasks show as `queued` on the board straight
away, but they're only *pending* until that rebuild runs — hit **Full rebuild** if you
want it applied now. If a `done`/`remove` doesn't match a real task, the agent leaves
the mail alone and tells you, rather than guessing.

## The three things you'll actually edit

**Add a task** — open `tasks.md`, add a line. Syntax is at the top of that file:

```
- [ ] Pay the car insurance  !1  @2026-09-04  #money
```

**Change what it reads** — open `config.yml`. Calendars you're not currently
tracking (Onaranlar, Batu_İBB, MFIST17, Toplantılar) are sitting commented out at
the bottom of the `calendars:` list; uncomment one to bring it in.

**Change the tone or the sections** — open `BRIEFING_PROMPT.md`.

All three take effect the next morning. Nothing else to restart.

## Changing the time

The schedule lives in the Routine, not in this repo — `local_time` in `config.yml`
is documentation only. To move it, ask Claude: *"move my morning briefing to 07:15"*.
Under the hood it's a cron in **UTC**, and Istanbul is UTC+3 year-round with no DST,
so `07:55` local is `55 4 * * *`.

## Reading the board

- **Counters** (top right) — events today, things awaiting your reply, deadlines
  inside a week. Colour is severity, not decoration.
- **Schedule** — today, then tomorrow. A red dot and a red note mean two events
  overlap. `NO REPLY YET` means you never answered the invite.
- **Needs you** — the short list of things blocking on you specifically, each with
  what happens if you ignore it.
- **Deadline ledger** — sorted by days remaining. A `CONFIRM DATE` chip means the
  agent inferred that date rather than reading it, so check it before trusting it.
  Each row carries one or two buttons that open the exact email, calendar event or
  route it refers to, so a countdown is one click from being dealt with. Links are
  only ever built from real data — a thread the agent read or an event link Google
  gave it — never guessed from a company name.
- **Inbox** — a red dot needs action, amber is worth watching, grey is resolved.

If you open the page on a later day, a banner at the top tells you how stale it is
rather than quietly showing you old countdowns.

## If a morning gets skipped

The Routine fires into a fresh cloud session; if that fails, nothing is lost —
the page just keeps yesterday's content and says so. Ask Claude *"rebuild my
dashboard now"* to run it on demand.
