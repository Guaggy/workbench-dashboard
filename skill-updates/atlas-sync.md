---
name: "atlas-sync"
description: "Use when Even says \"sync\", \"atlas sync\", \"catch up Atlas\", \"check updates\" or \"ask me what you need\" — processes the Updates dump, gathers open questions and loose ends, interviews him, writes answers into the vault."
---

# Atlas sync

The reverse of the live-update habit: instead of Atlas pushing what it learns into the vault, Atlas pulls what it *doesn't* know out of Even. Gather every open question, stale item and unprocessed dump note, ask him in quick batches, and write each answer to the right vault file immediately. The goal is that afterward the vault and Atlas's memory are fully caught up.

Vault: `C:\AI\Atlas` on guags-pc. Follow the standing vault rules in `Guide.md` — in particular, **never write to the vault through Google Drive** (it caused a corruption once). Write via `device_bash` when it works, otherwise stage → edit → `device_commit_files` with `expectedMtimeMs`. If no device is linked, read via Drive, run the interview anyway, and hold all writes as a clear list at the end instead of writing them.

## Modes

- **Quick** (default when he just says "sync"): the top ~5 items by priority, about 2 minutes.
- **Full** ("full sync"): everything open.
- **Topic** ("sync UWA", "sync Omni Chair", "sync PC"): only items touching that area.

## 1. Gather (read, don't ask yet)

Read `Main.md` first, then collect candidates from:

1. **Updates/ folder**: every file in `Updates/` plus any text below the header in `Updates/Updates.md`. Follow the processing rules in that file's header: treat the content as rough input, route each item to the right place (context / projects / decisions / tasks / overengineering / a `Projects/`, `Hobbies/`, `Personal/` or `Coursework/` note / Claude memory), and update existing entries rather than duplicating. File the clear items without asking. Only ambiguous, incomplete or no-obvious-home items become questions.
2. **`meta/questions.md`**: the question queue other Atlas sessions and scheduled tasks append to (create it if it's missing; format below).
3. **Pending approvals**: "Flagging for Even" items in the newest `meta/self-review.md` entry, and "needs your approval" in the newest Weekly Review under `Claude outputs/`, minus anything already resolved in `meta/decisions.md`.
4. **Gmail Needs Attention**: the "Flagged for Even" list in `meta/gmail-triage-notes.md`, especially items older than ~2 weeks.
5. **`meta/tasks.md` Open**:
   - items that look stale (no update in ~14+ days): ask done / still relevant / drop?
   - items with a gap in them ("no due date yet", "not yet confirmed", "awaiting reply", "to be configured later")
   - deadlines in the next 14 days with no sign they've been done
   - items the vault itself shows are already done (e.g. a cleanup task for files that no longer exist): propose closing them
6. **Project gaps**: `meta/projects.md` + `Projects/*.md` entries with no next step, marked stale in the last Weekly Review, or missing key facts (deadline, scope, group members).
7. **Workbench dashboard**: run `python C:\Users\evenm\.bench\pull_bench.py` via `device_bash`. It copies `Dashboard/status.md` into the vault and prints habit/focus log entries. File the entries into today's diary entry silently, then run it with `--done`. File errors listed in `status.md` get fixed silently, following `Dashboard/README.md`. A dashboard that is offline or a service that's down becomes one question.
8. **Contradictions**: vault vs. Claude memory (e.g. a project finished in one and active in the other), or `meta/projects.md` vs. its project note.

Anything that can be settled from evidence (calendar, email, file state) gets settled silently. Don't ask Even what Atlas can check itself.

## 2. Prioritize

1. Deadlines in the next 7 days, and anything with real risk (domain expiry, security alerts, possibly unauthorized logins)
2. Pending approvals
3. Ambiguous Updates items (so the dump zone can be cleared)
4. Gaps blocking an active project
5. Stale tasks and cleanup

Merge duplicates, since the same issue often shows up in tasks.md, the Weekly Review and gmail notes at once.

## 3. Interview

- Start with one line: how many items were found, how many Atlas already handled from Updates/, and the mode.
- Use `AskUserQuestion` in batches of up to 4 questions. Prefer concrete multiple-choice options with a recommended default first (e.g. "Done ✓ / Still open / Drop it / Snooze"). Free text comes through "Other" when needed.
- Every question needs one line of context so it can be answered cold (what the item is and why it's being asked). No long explanations.
- Challenge answers that contradict an earlier decision or look like a bad call. This is not a rubber stamp.
- Follow Even's language (Norwegian or English).
- Between batches, write the answers already given. Don't hold everything until the end.
- He can stop at any time ("that's enough", "stop"). Anything unanswered stays in the queue.

## 4. Write back

- Route each answer to its real home, edited in place: decisions → `meta/decisions.md` (dated), status → `meta/projects.md` + the project note, to-dos → `meta/tasks.md` (Open/Done), context changes → `meta/context.md`, durable personal facts → Claude memory.
- **Workbench dashboard:** if deadlines, todos or focus changed, update `Dashboard/dates.md` / `Dashboard/today.md` in their exact format (see `Dashboard/README.md`). Leave events, pictures and `config.md` alone; `end-day` owns those.
- Resolved questions: remove them from `meta/questions.md` (the decision/task entry is the record).
- **Skipped/snoozed**: stay in `questions.md` with `skips` incremented. At 3 skips, the next sync asks "drop this?" instead of asking the question again.
- **Updates/**: remove integrated text from `Updates.md`, leaving its header and anything unhandled. Fully integrated files in `Updates/` should be deleted. With a working `device_bash`, ask for delete permission once for the vault folder. Otherwise list them for Even to delete himself (file tools can't delete). Never delete a file whose content isn't fully integrated elsewhere.
- Anything under the standing confirm-first rules (calendar events, emails, purchases, cancellations, deleting important files, account/system changes, new/removed connectors, skills or scheduled tasks) is **not done in the sync**. Present it as a proposed action and get an explicit yes first.

## 5. Close out

A short changelog, not a recap:
- **Updated:** file → what changed (one line each)
- **Still open:** count + the top 1-3 left
- **Needs you outside Atlas:** things only Even can do (delete files, reply to someone, log in somewhere)
- **Verdict:** "Vault is caught up" or "N items left, next sync will start with X"

## `meta/questions.md` format

```
# Questions for Even

Queue of things Atlas needs Even to answer. Any Atlas session or scheduled task appends here instead of guessing; `atlas-sync` works through it. Remove an entry once it's answered and filed.

## Open
- [2026-09-24] (source: Weekly Review) Omni Chair report: what's the scope and deadline? → Projects/Omni Chair.md | skips: 0
```

One line per question: date added, source, the question, where the answer should go, and the skip count. Don't add a question that's already queued or already answered in the vault.

## Verification

Before the close-out, re-read each file that was written to and confirm the edits landed (commit results / mtime changed), with no duplicate entries and nothing left half-processed in `Updates/`. Never report something as updated if the write failed. List it under "Still open" with the reason instead.