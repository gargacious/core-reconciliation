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
