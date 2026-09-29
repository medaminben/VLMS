# Frontend redesign — Beta → main release preparation

Prepared 2026-09-29 on branch `redesign/6-docs-release` (worktree
`.worktrees/redesign-1-foundations`). **Preparation only** — do not open the
Beta→main PR, tag, or merge until the owner confirms the tag process.

## Suggested next tag

| Item | Value |
|---|---|
| Latest tag | `v1.2.0` (`git describe --tags --abbrev=0`) |
| Suggested next minor | **`v1.3.0`** |

**Owner must confirm the tag name before merge.** Tag and push
`v1.3.0` (or the owner-chosen next minor) **before** merging `Beta` → `main`,
so `release.yml` does not invent the bump level.

## Checklist

Use-case coverage (22/22 rows with named passing tests):

[`docs/superpowers/plans/2026-09-29-frontend-redesign-checklist-status.md`](2026-09-29-frontend-redesign-checklist-status.md)

Also verified on this branch (see checklist status doc):

- `ctest -L ui` → 375/375 PASS
- `ctest -L bench` → 1/1 PASS

## Ordered merge path

Phases 1–5 are ancestors of `redesign/6-docs-release`, but **none** of the
phase branches are in `Beta` yet. Merge in this order (or equivalent stacked
PRs that land the same tip on `Beta`):

1. `redesign/1-foundations` → `Beta`
2. `redesign/2-workflows` → `Beta`
3. `redesign/3-model-list-page` → `Beta`
4. `redesign/4-dialogs` → `Beta`
5. `redesign/5-harmonisation` → `Beta`
6. `redesign/6-docs-release` → `Beta`
7. Confirm CI green on `Beta` (`default` + `sanitizers`)
8. **Owner confirms tag** → create and push tag `v1.3.0` (or confirmed name)
9. Open PR `Beta` → `main` (owner-gated; draft below)
10. Merge `Beta` → `main` only after the tag is pushed

Practical alternative if phase PRs are already reviewed as a stack: merge
`redesign/6-docs-release` → `Beta` once (brings Phases 1–6), then continue from
step 7.

## Draft PR: Beta → main

**Do not run `gh pr create` until the owner confirms the tag process.**

**Title:** `Release: frontend redesign (minor)`

**Body:**

```markdown
## Summary
- Frontend redesign Phases 1–6 on Beta: ListPage/specs/workflows, Lookups, LanguageChange, layer folders, manual updates.
- Use-case coverage checklist completed with named passing tests (see docs/superpowers/plans/2026-09-29-frontend-redesign-checklist-status.md or Phase 6 plan appendix).

## Test plan
- [ ] CI green on Beta (default + sanitizers)
- [ ] ctest -L ui
- [ ] ctest -L bench
- [ ] Spot-check manual en/fr/ar screenshots
- [ ] Tag v1.3.0 pushed before merge
```

**Suggested command (owner only, after tag confirmation):**

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
- [ ] Tag v1.3.0 pushed before merge

EOF
)"
```

## Owner gate

- [ ] Phases 1–6 merged into `Beta`
- [ ] CI green on `Beta` (`default` + `sanitizers`)
- [ ] Checklist status doc still complete (22/22 PASS)
- [ ] **Owner confirms tag** (suggested `v1.3.0`)
- [ ] Tag pushed **before** merge
- [ ] Then open and merge `Beta` → `main`
