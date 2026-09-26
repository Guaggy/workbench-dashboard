---
name: "end-day"
description: "Use when Even says \"end day\", \"sign off\", \"done for today\" or runs /end-day on his PC after school — logs a short diary entry, nags about typing practice, closes out the day in the vault, updates the workbench dashboard for tomorrow, and runs PC-only jobs."
---

# End day

Even runs this by hand on guags-pc when his day is done. It's the one moment each day when the PC is definitely on and he's at it. Use that window for things that need both: a diary entry, the day's vault catch-up, and jobs that can only run on the machine. It should take about 5 minutes. It is a sign-off, not a review meeting.

Vault: `C:\AI\Atlas`. Standing vault rules from `Guide.md` apply: write directly on the PC (device_bash if it works, otherwise stage → edit → `device_commit_files` with `expectedMtimeMs`), **never through Google Drive**. If no device is linked, say so in one line, do the diary and typing check in chat, and list the writes for the next PC session.

## 1. Diary (first, while the day is fresh)

Ask 2-3 short questions in one `AskUserQuestion` call or in plain chat. Keep them light and vary them a bit day to day, for example:
- How was today, in a line or two?
- What got done, and what's still hanging?
- Anything on your mind for tomorrow?

Offer a "skip today" option. Don't push if he skips. Write his answers in his own words, lightly cleaned up, to `Personal/Diary/YYYY-MM-DD.md` (create the folder and a `Personal/Diary/Diary.md` index linked from `Main.md` the first time). Language follows his answers (Norwegian or English). The diary is his, so don't add Atlas commentary to it.

## 2. Typing practice check

First pull the workbench dashboard's logs (habit buttons and focus sessions): run `python C:\Users\evenm\.bench\pull_bench.py` via `device_bash` (it also copies `Dashboard/status.md` into the vault). It prints JSON lines such as `{"type":"habit","name":"Typing","date":…,"streak":5}` and `{"type":"focus","project":…,"minutes":25,"date":…}`, or "no new entries". If today's log already has a `Typing` habit entry, typing is done: log `typing: ✓ (dashboard, streak N)` and skip the question. Otherwise:

He asked to be reminded aggressively, because he keeps forgetting. Ask directly: "Did you do 10 min on EdClub today?"
- **Yes:** log `typing: ✓` in the diary entry and move on.
- **No:** push once, concretely: "Do 10 minutes now before you close the laptop, it's the only way the habit sticks." Offer to wait. Log `typing: ✗` if he still skips, and mention the current miss streak (count back through recent diary entries) if it's 2+ days.

## 3. Close out the day in the vault

- Read `meta/tasks.md`, `meta/questions.md` and today's plus tomorrow's calendar.
- Anything from the diary answers that's vault-worthy (a finished task, a new deadline, a decision, a project status change) gets filed right away under the normal live-update rules: edit in place, no duplicates. Close tasks he says are done.
- If `meta/questions.md` has open items, ask at most the top 2-3 (same format and rules as atlas-sync). Leave the rest for a real sync.
- If `Updates/Updates.md` has content below its header, process it per its own rules.

## 4. Workbench dashboard

The dashboard above the bench is fed from the vault's `Dashboard/` folder. **Read `Dashboard/README.md` first** for the file formats.

- **Status:** read `Dashboard/status.md` (just copied by `pull_bench.py`). If it lists file errors, fix those files now. Mention in the sign-off if the dashboard is offline or a service is down.
- **Logs:** add the pulled entries to today's diary entry as one or two lines (e.g. `habits: Exercise ✓, Read ✓` and `focus: 50 min ESP32-S3 Dashboard`). Once the diary write is confirmed, run `python C:\Users\evenm\.bench\pull_bench.py --done`. If the write failed, don't clear.
- **`today.md` for tomorrow.** Set `updated:` to **tomorrow's** date.
  - **Message:** the most useful nudge.
  - **Focus:** the main project.
  - **Todos:** 3-5 concrete ones.
  - **Events:** 2-4 timed nudges, e.g. before a class, the typing reminder, and the 22:45 wind-down. Calendar events already get an automatic heads-up, so don't duplicate them.
- **`dates.md`:** deadlines in the next ~30 days (max 8) from `meta/tasks.md` and project notes; countdowns and space events worth tracking.
- **A new pixel-art picture:** `images/<tomorrow>-<name>.txt`, 32×16, themed on tomorrow. The format is in README.
- **Now and then:** add a quote (real, correct author), facts (true) and fortunes to their files. Optionally add an online picture to `images/links.md`.
- **`config.md`:** only change it when Even asks.

## 5. PC-only jobs

This is the window for things that need the machine. Check `meta/tasks.md` for items that are blocked on the PC or the device shell (e.g. Simkl updates using the local credentials, file deletions, cleanup, anything marked "once the device shell works"). Test `device_bash` once. If it works, do the pending low-risk ones and mark them done. If it's still broken, say so in one line and leave them. Anything under the standing confirm-first rules (deletions of important files, account/system changes, purchases, emails, calendar edits) is proposed, not done, and needs his explicit yes.

## 6. Sign-off

End with at most 5 lines:
- **Logged:** what got written (diary, dashboard logs, `Dashboard/` for tomorrow, any vault changes)
- **Tomorrow:** first event and anything due within 48h
- **Left for later:** only if something couldn't be done (e.g. device shell still broken)

Then stop. No motivational filler, no recap of the whole day.

## Verification

Before signing off, confirm each vault write landed (commit result / mtime changed, content re-read if a concurrent edit is possible). Never say something was logged if the write failed.