# core-reconciliation

`core-reconciliation` is the participant-side economic evidence reconciler.
It consumes immutable canonical execution, correction and bust facts from
independently sequenced primary, order-session, Drop Copy and clearing streams.

The package deliberately does not own orders, fills, positions or clearing.
It maintains source-local contiguous cursors, deduplicates by stable economic
identity, exposes uncertainty and conflicts, and publishes a common
business-time completeness watermark only while every required source is
fresh, healthy and gap-free.

The public causal-support index is a separate diagnostic projection. It keeps
bounded references to source-owned evidence and answers versioned C12 queries
only while every required source reports a current watermark. Results preserve
the exact evidence owner, configuration, sequence domain, cursor, durable
offset, time, privacy class and payload digest. A completed query means only
that the retained metadata search completed; it is never an execution, trade,
position, settlement or reconciliation assertion. Principal authorization is
the responsibility of the upstream visibility-policy owner.

The FMI reconciliation contract adds a separate, application-neutral view over
venue trade, clearing, obligation, position, CSD, custodian, settlement-bank
cash and client/broker evidence. Every observation retains its source owner,
generation, sequence domain, journal position, authorization and payload
digest. Exact provider/asset/currency/jurisdiction tolerance policy is required.
A missing or inconsistent fact opens a versioned exception case; maker/checker
resolution governs that case and cannot edit any source owner's economic fact.
FMI scope and tolerance records use the authorization contract's
`FmiAssetClass`; the distinct static-instrument-metadata `AssetClass` is not
redeclared or reinterpreted here.

The partner-operations surface publishes bounded stable pages over exact
reconciliation scope, evidence custody, independent source completeness,
breaks, cases and resolution audit. Governed propose, approve and escalation
commands consume the neutral partner-operations command contract and retain
the current authorization decision/generation plus payload-bound approval
reference. Expiry, revocation, stale generation/version, conflicting
idempotency and evidence mismatch fail closed before case mutation. Resolution
continues to affect only the case; source-owned economic evidence is never
rewritten. No durable owner snapshot/restore is published, so
destroyed/reconstructed reconciliation-owner equivalence is not qualified.

Package 0.1.3 publishes `core-reconciliation.public-package` revision 5 and
retains `core-reconciliation.fmi-reconciliation` revision 1 while adding
`core-reconciliation.fmi-partner-operations` revision 1. It requires
`trading-contracts` 0.1.1/public revision 1 with
`fintech.contracts.fmi-authorization` revision 2 and
`fintech.contracts.fmi-partner-operations` revision 1.
