---
name: "atlas"
description: "Loads Even's Atlas context (Obsidian vault + preferences) at the start of any Atlas-related session and keeps it updated live as things happen."
---

# Atlas

## Trigger
- Session attached to the "Atlas" or "Atlas - config" Claude project
- Even asks about his studies, active projects, decisions, schedule, or exchange planning
- Even references Atlas, the vault, or says "catch me up", "log this decision", "new project", or similar

## Steps
1. Load current context before answering or acting:
   - If a local device is linked and the Atlas folder (C:\AI\Atlas) is connected, read meta/context.md, meta/projects.md, meta/decisions.md and meta/tasks.md directly.
   - Otherwise read the same files via the Google Drive connector (Drive folder "Atlas").
   - If neither is available, fall back to Cowork memory (/profile.md, /preferences.md, /areas/uwa-exchange.md, /areas/atlas.md) for a condensed version of the same context.
2. Follow the "Stable preferences" section of meta/context.md for communication style, decision-making style, and autonomy/confirmation rules — notably: free to edit the vault without asking; always confirm before calendar edits, sending email, purchases/payments, deleting files, cancelling bookings/subscriptions, or major config changes; challenge bad or risky decisions; state assumptions and alternatives explicitly when acting on incomplete information.
3. During the conversation, update the vault live whenever something vault-worthy comes up — a decision made, a project status change, a new deadline, or a material change to current focus/circumstances. Edit the right file in place (meta/decisions.md, meta/projects.md, or meta/context.md) without asking first, and edit existing entries rather than duplicating.
4. When a deadline, a to-do for today or the current focus project changes, also update `Dashboard/dates.md` / `Dashboard/today.md` (they feed the workbench dashboard; formats in `Dashboard/README.md`). When Even asks to change the dashboard's buttons, habits, light presets, night hours or pages, edit `Dashboard/config.md`. After editing anything in `Dashboard/` from the PC, press Refresh: `python C:\Users\evenm\.bench\bench_pub.py shortcut '{"name":"Refresh"}'`. Atlas can also message the dashboard directly: `python C:\Users\evenm\.bench\bench_pub.py notify '{"title":"Atlas","text":"...","color":"#50a0ff","anim":"pulse","secs":10}'`, or take over the light with `led/set` (`{"mode":"solid"|"off"|"auto","color":"#rrggbb","brightness":0-255}`).
5. Log loose to-dos, reminders or action items that don't belong to a specific project or decision to meta/tasks.md (create it if missing) instead of letting them drop.
6. When something needs Even's input but can't be asked right now (he's gone, it's off-topic, or it came up in a scheduled run), append it to meta/questions.md (one line: [date] (source) question → where the answer goes | skips: 0) instead of guessing or dropping it. Don't add duplicates. The atlas-sync skill works through this queue.
7. When asked to "catch up" or similar, summarize current focus and anything that's changed, optionally pulling in today's calendar/email if relevant. If Updates/ has unprocessed content or meta/questions.md has open items, mention the count in one line and offer a sync.

## Verification
- Before ending a session that surfaced a decision, a project-status change, a new deadline, or a loose task, confirm it was actually written to the vault (or Cowork memory as fallback) — or explain why it wasn't (e.g. no device/Drive access this session).