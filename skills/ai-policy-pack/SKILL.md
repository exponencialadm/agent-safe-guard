---
name: ai-policy-pack
description: "Use when drafting or applying a practical AI-agent governance policy pack: approved agent inventory, skill provenance, MCP/tool approvals, editor extensions, secret access, evidence retention, exceptions, incidents, and ISO 27001/42001 mappings."
metadata:
  short-description: Draft AI-agent governance policies
---

# AI Policy Pack

Use this skill to create a control baseline for companies adopting AI agents in development, IT, security, and compliance workflows.

## Safety Rules

- Keep policy text practical and enforceable. Avoid vague "use AI responsibly" language without an owner, evidence, and review cadence.
- Policies must not ask agents to read secrets. Enforce at the point of action.
- Any exception must have owner, expiration, business reason, and compensating control.

## Workflow

1. Render the baseline pack:

```bash
node skills/ai-policy-pack/scripts/render-policy-pack.mjs --format md --output ai-agent-policy-pack.md
```

2. Tailor it to the company:

- Replace generic owners with real roles.
- Set review cadence, evidence retention, allowed tools, and approval flow.
- Map controls to ISO 27001, ISO 42001, LGPD/privacy, and internal audit.

3. Connect enforcement:

- Use `$agent-inventory` to discover what exists.
- Use `$extension-risk-audit` for extension risk.
- Use `$agent-safe-guard` for local hook enforcement.
- Use `$evidence-sync` to package evidence.

## Baseline Controls

The reference policy pack lives at `references/policy-pack.json` and includes controls for inventory, skill provenance, extension risk, MCP/tool approval, secret access, evidence, exceptions, and incidents.
