# Agent instructions

## Scope

Own the participant-side independent economic reconciliation projection across
primary, order-session, Drop Copy and clearing evidence streams.

## Boundaries

- Depend only on public `trading-contracts` and `trading-platform-sdk` APIs.
- Publish canonical `<fintech/...>` contracts and the exported
  `fintech::core_reconciliation` target.
- Do not own OMS order state, exchange matching, positions, settlement,
  clearing decisions or transport-session recovery.
- Treat source sequences as independent completeness evidence; deduplicate
  economics by stable business identity, never by a numeric source sequence.
- Fail closed on gaps, stale/unhealthy sources, owner-generation changes,
  sequence reuse and conflicting economics.

## Workflow

Read `TASKS.md`, implement one bounded task, run the focused commands in
`docs/COMMANDS.json`, then exactly one full repository suite. Commit with the
task ID. Record durable-service, hosted and production-operational evidence as
unavailable unless it was actually produced.
