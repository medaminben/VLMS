# Frontend Redesign Phase 6: Docs and Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Update the user manual in English, French, and Arabic for the harmonised menus, shortcuts, and single loan history dialog; regenerate screenshots; complete the use-case coverage checklist with named passing tests so `Beta` can merge to `main` as a minor release.

**Architecture:** Documentation and release hygiene only — no feature code. Manual Markdown under `docs/manual/content/{en,fr,ar}/` stays the source; `scripts/manual/capture.sh` regenerates `docs/manual/assets/`. The checklist in the design spec is filled with concrete GoogleTest filter names that are green on `Beta`.

**Tech Stack:** Existing manual pipeline (`VLMS_MANUAL_CAPTURE`, `manual_capture`, `prepare_sandbox.py`), CTest labels `ui` / `presentation` / `bench` / workflows, git/GitHub PR into `main`.

**Spec:** `docs/superpowers/specs/2026-09-29-frontend-redesign-design.md` (Phase 6 + Beta→main).

**Branch:** `redesign/6-docs-release` (from `Beta` after Phase 5 merges).

**Worktree:** `<repo>/.worktrees/redesign-1-foundations`.

## Global Constraints

- No new product features. Visible doc changes only for menus, shortcuts, and the unified loan history dialog (plus regenerated screenshots).
- Manual text must exist in **en**, **fr**, and **ar** for every edited section.
- Every checklist row names a passing test; do not mark a row done without running that test.
- Version: minor bump. Tag `vX.(Y+1).0` by hand and push the tag **before** merging `Beta` → `main`, so `release.yml` does not invent the level (owner/release operator; this plan prepares the PR and checklist).
- Backend/schema unchanged. `core-only` builds unaffected.
- Commits optional per task for SDD hygiene.

---

## File map

| Path | Responsibility |
|---|---|
| `docs/manual/content/en/reference.md` | Shortcuts, context menus, shared list actions |
| `docs/manual/content/en/catalogue.md` | Loans button / loan history dialog wording |
| `docs/manual/content/en/members.md` | Same for members |
| `docs/manual/content/en/circulation.md` | Checkout/return/extend + shortcuts |
| `docs/manual/content/en/archive.md` | Restore/purge/reuse + history |
| `docs/manual/content/en/getting-started.md` | Tick/bulk + Delete = archive note if shortcut text changes |
| `docs/manual/content/fr/*.md` | French equivalents of the same sections |
| `docs/manual/content/ar/*.md` | Arabic equivalents |
| `docs/manual/assets/**` | Regenerated screenshots (en/fr/ar) |
| `docs/superpowers/specs/2026-09-29-frontend-redesign-design.md` | Fill Test column with final names if any drifted |
| `docs/superpowers/plans/2026-09-29-frontend-redesign-phase6-docs-release.md` | This plan; optionally append a completed checklist table |
| PR `Beta` → `main` | Release PR description embeds the completed checklist |

---

### Task 1: Document shared menus and shortcuts (en)

**Files:**
- Modify: `docs/manual/content/en/reference.md`
- Modify: `docs/manual/content/en/getting-started.md` if it describes actions without shortcuts

Add a concise subsection (exact heading style matching neighbouring `##` / `###` in `reference.md`) covering:

| Action | Shortcut | Notes |
|---|---|---|
| Add / Checkout | Ctrl+N | Page-scoped; Checkout on Circulation |
| Edit | Enter | Table-scoped; inactive on Circulation |
| Loans | Ctrl+L | Table-scoped |
| Archive / Restore | Del | Table-scoped; Archive on live lists, Restore on Archive |
| Purge | Shift+Del | Archive only |
| Tick all | Ctrl+A | Table-scoped (search box keeps Ctrl+A for text) |
| Refresh | F5 | Page-scoped |

State that the context menu on every list offers the same actions as the button pad, in the same order. State that Enter in the search box still runs search immediately; Del in the search box still edits text.

- [ ] **Step 1: Read current `reference.md` and locate the best insertion point (near Delete/archive explanation ~line 85)**
- [ ] **Step 2: Write the English subsection with the table above in Markdown**
- [ ] **Step 3: Proofread against ListPage / spec — no invented shortcuts**
- [ ] **Step 4: Commit (optional)** `docs(manual): document shared list shortcuts (en)`

---

### Task 2: Document loan history dialog (en)

**Files:**
- Modify: `docs/manual/content/en/catalogue.md`
- Modify: `docs/manual/content/en/members.md`
- Modify: `docs/manual/content/en/archive.md`
- Modify: `docs/manual/content/en/circulation.md` if it cross-links history

Replace wording that implies three different loan windows with one loan history dialog: same list chrome, pinned to the book/member/copy, with checkout/return/extend when allowed. Keep task pages (`task-lend-book.md`, `task-return-extend.md`) accurate if they name old dialog titles.

- [ ] **Step 1: Grep manual for old phrasing**

```bash
grep -nE 'Book loans|Member loans|loan history|Loans dialog|Loan history' \
  docs/manual/content/en/*.md
```

- [ ] **Step 2: Edit each hit so it describes the unified dialog**
- [ ] **Step 3: Commit (optional)** `docs(manual): describe unified loan history dialog (en)`

---

### Task 3: Translate the same sections (fr, ar)

**Files:**
- Modify: `docs/manual/content/fr/reference.md`, `catalogue.md`, `members.md`, `archive.md`, `circulation.md`, `getting-started.md` as needed
- Modify: `docs/manual/content/ar/...` same set

Rules:

- Match structure and tables from English; do not leave English paragraphs in fr/ar for edited sections.
- Preserve RTL-friendly punctuation in Arabic; do not reverse Markdown table pipes.
- Reuse existing translated UI terms from the string catalogue where the manual already uses them (Add/Edit/Archive labels).

- [ ] **Step 1: Port Task 1–2 edits into fr**
- [ ] **Step 2: Port Task 1–2 edits into ar**
- [ ] **Step 3: Diff section headings across en/fr/ar for parity**

```bash
for lang in en fr ar; do
  echo "== $lang =="; grep -nE '^## |^### ' docs/manual/content/$lang/reference.md | head -40
done
```

- [ ] **Step 4: Commit (optional)** `docs(manual): translate shortcut and loan-history updates (fr, ar)`

---

### Task 4: Regenerate screenshots

**Files:**
- Regenerate: `docs/manual/assets/**` via capture tool
- Modify: none required unless filenames change (prefer stable names)

Commands (from repo root, as in `scripts/manual/capture.sh`):

```bash
./scripts/manual/capture.sh
```

This configures `build-manual` with `-DVLMS_MANUAL_CAPTURE=ON`, builds `manual_capture`, prepares a sandbox from `database/vlms.db`, and writes shots for `ar`, `en`, `fr` into `docs/manual/assets`.

If capture fails on missing UI object names after the redesign, fix **object names** in views to match `manual_capture` expectations (minimal code fix allowed), or update capture scripts under `applications/vlms/manual_capture/` to new names — document which in the commit message.

- [ ] **Step 1: Run `./scripts/manual/capture.sh` — Expected: exit 0**
- [ ] **Step 2: Visually spot-check a catalogue, members, circulation, archive shot per language**
- [ ] **Step 3: `git status docs/manual/assets` — stage regenerated binaries**
- [ ] **Step 4: Commit (optional)** `docs(manual): regenerate screenshots after redesign`

---

### Task 5: Complete use-case coverage checklist with named tests

**Files:**
- Modify: `docs/superpowers/specs/2026-09-29-frontend-redesign-design.md` — update the Test column if names drifted during Phases 3–5
- Create: `docs/superpowers/plans/2026-09-29-frontend-redesign-checklist-status.md` **or** append a "Completed checklist" section at the bottom of this Phase 6 plan

For each row, run the named test and record PASS:

| # | Use case | Required test name pattern | Run command |
|---|---|---|---|
| 1 | Search/filter/sort/page books | `BookListSpec.*` | `ctest -R 'BookListSpec' --output-on-failure` |
| 2 | Preview cover | `BookListSpec.Preview*` | `ctest -R 'BookListSpec.Preview' --output-on-failure` |
| 3 | Add/edit book | `BookWorkflow.Add*` `BookWorkflow.Edit*` | `ctest -R 'BookWorkflow\.(Add\|Edit)' --output-on-failure` |
| 4 | OCR cover | `BookEditorDialog.Ocr*` | `ctest -R 'BookEditorDialog.Ocr' --output-on-failure` |
| 5 | Lookups completion | `BookEditorDialog.Lookups*` | `ctest -R 'BookEditorDialog.Lookups' --output-on-failure` |
| 6 | Categories | `BookWorkflow.Categories*` | `ctest -R 'BookWorkflow.Categories' --output-on-failure` |
| 7 | Archive books | `BookWorkflow.Archive*` | `ctest -R 'BookWorkflow.Archive' --output-on-failure` |
| 8 | Book loan history | `LoanHistoryDialog.Book*` | `ctest -R 'LoanHistoryDialog.Book' --output-on-failure` |
| 9 | Members list | `MemberListSpec.*` | `ctest -R 'MemberListSpec' --output-on-failure` |
| 10 | Add/edit member | `MemberWorkflow.Add*` `MemberWorkflow.Edit*` | `ctest -R 'MemberWorkflow\.(Add\|Edit)' --output-on-failure` |
| 11 | Archive blocked → Navigator | `MemberWorkflow.ArchiveBlocked*` | `ctest -R 'MemberWorkflow.ArchiveBlocked' --output-on-failure` |
| 12 | Member loan history | `LoanHistoryDialog.Member*` | `ctest -R 'LoanHistoryDialog.Member' --output-on-failure` |
| 13 | Loan filters | `LoanListSpec.Filter*` | `ctest -R 'LoanListSpec.Filter' --output-on-failure` |
| 14 | Checkout/return/extend | `LoanWorkflow.*` | `ctest -R 'LoanWorkflow' --output-on-failure` |
| 15 | Archive loans | `LoanWorkflow.Archive*` | `ctest -R 'LoanWorkflow.Archive' --output-on-failure` |
| 16 | Archived lists | `Archived*Spec.*` | `ctest -R 'Archived(Member\|Book\|Copy\|Loan)Spec' --output-on-failure` |
| 17 | Restore/purge | `ArchiveWorkflow.Restore*` `ArchiveWorkflow.Purge*` | `ctest -R 'ArchiveWorkflow\.(Restore\|Purge)' --output-on-failure` |
| 18 | Reuse number | `ArchiveWorkflow.ReuseNumber*` | `ctest -R 'ArchiveWorkflow.ReuseNumber' --output-on-failure` |
| 19 | Archived loan history | `LoanHistoryDialog.Archived*` | `ctest -R 'LoanHistoryDialog.Archived' --output-on-failure` |
| 20 | Metrics dashboard | `DashboardPage.*` or `MetricsPage.*` | `ctest -R 'MetricsPage\|DashboardPage' --output-on-failure` |
| 21 | LanguageChange RTL | `LanguageChange.*` | `ctest -R 'LanguageChange' --output-on-failure` |
| 22 | Theme/manual/licence | existing ported tests | `ctest -R 'Theme\|Licence\|Manual' --output-on-failure` |

If a pattern matches nothing, **stop** and add the missing test on a fix branch before claiming the checklist complete — do not rename the checklist to hide gaps.

Also run full suites:

```bash
cd build && ctest -L ui --output-on-failure
cd build && ctest -L bench --output-on-failure
```

- [ ] **Step 1: Run each row's ctest command; paste PASS lines into the checklist status doc**
- [ ] **Step 2: Fix any missing tests (code) until green — still on Phase 6 only if trivial; otherwise open a fix PR into Beta first**
- [ ] **Step 3: Update the design spec Test column to the final exact names**
- [ ] **Step 4: Commit (optional)** `docs: complete frontend redesign use-case checklist`

---

### Task 6: Beta → main release PR preparation

**Files:**
- None required in-repo beyond checklist doc; use `gh` when the owner asks to open the PR

Preparation steps (execute when requested):

1. Ensure `Beta` contains Phases 1–6 merges and CI is green (`default` + `sanitizers`).
2. Confirm checklist status doc is complete.
3. Determine next minor version from latest tag (`git describe --tags --abbrev=0`).
4. Owner tags `vX.(Y+1).0` and pushes the tag **before** merge per spec.
5. Open PR:

```bash
gh pr create --base main --head Beta --title "Release: frontend redesign (minor)" --body "$(cat <<'EOF'
## Summary
- Frontend redesign Phases 1–6 on Beta: ListPage/specs/workflows, Lookups, LanguageChange, layer folders, manual updates.
- Use-case coverage checklist completed with named passing tests (see docs/superpowers/plans/2026-09-29-frontend-redesign-checklist-status.md or Phase 6 plan appendix).

## Test plan
- [ ] CI green on Beta (default + sanitizers)
- [ ] ctest -L ui
- [ ] ctest -L bench
- [ ] Spot-check manual en/fr/ar screenshots
- [ ] Tag vX.(Y+1).0 pushed before merge

EOF
)"
```

- [ ] **Step 1: Verify Beta CI green**
- [ ] **Step 2: Draft PR body with embedded checklist summary**
- [ ] **Step 3: Commit any remaining doc fixes on `redesign/6-docs-release` and merge that PR into Beta first**
- [ ] **Step 4: Open Beta→main only when the owner confirms the tag process**

---

## Verification (phase exit)

```bash
./scripts/manual/capture.sh
cd build && ctest -L ui --output-on-failure
cd build && ctest -L bench --output-on-failure
# Checklist status doc: 22/22 rows name a PASSING test
```

Open PR `redesign/6-docs-release` → `Beta`, then (separately, owner-gated) `Beta` → `main`.

---

## Self-review (Phase 6)

| Spec item | Task |
|---|---|
| Manual en/fr/ar for menus, shortcuts, loan history | 1–3 |
| Regenerated screenshots | 4 |
| Completed checklist with named passing tests | 5 |
| Beta→main minor tag process | 6 |
| No feature code | Global constraints |

**Gap note:** If Phase 3–5 test names drifted from the patterns above, Task 5 must rename tests or update the checklist — never leave a row without a runnable name.
