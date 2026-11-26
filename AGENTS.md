## Cross-repo handoffs

This session works only in this repository. Do not edit another repo directly.

### Incoming work

Incoming work from another repo is a GitHub issue **in this repo** labelled
`handoff:from-<source>` and `ready-for-agent`.

At session start, list it with:

```bash
gh issue list \
  --state open \
  --label ready-for-agent \
  --search 'label:handoff:from-master,handoff:from-hasc' \
  --json number,title,body,labels \
  --jq '.[] | "#\(.number) \(.title)\n\(.body)\n"'
```

The issue body is the contract. It must contain:
- Source: <repo>#<number>
- Needed change:
- Version axis:
- Acceptance:
- Blocked by:
If any of those are missing, add needs-info, comment with what is missing,
and stop. Do not infer a cross-repo contract from memory.
Claim the issue before working:
gh issue edit <number> --add-assignee @me
Do not start if the issue has an open Blocked by: line.

### Outgoing work

If this task requires a change in another repo, create a self-contained issue
there with the same five fields above. Do not silently edit or version-bump
the other repo.
Link both directions:
- in the child issue: Part of <repo>#<number>
- in this issue: Handoff: <repo>#<number>
Do not close this issue until the child issue reports its commit, version, and
validation result.

### Final report

End every handoff task with:
Repo: <repo>
Issue: #<number>
Commit: <sha>
Version: <version>
Validation: <commands run>
Handoff in: <repo>#<number>
Blocked by: <repo>#<number> or none
GitHub notifications are not the queue. The labelled issue in the target repo is the queue.

## Agent skills

### Issue tracker

Issues and specs for this repo live in GitHub Issues (`H0u0uinKy0um4/SplFlp_Slave_Attiny`), managed with the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

The five canonical triage roles map to same-named GitHub labels: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `GLOSSARY.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.
