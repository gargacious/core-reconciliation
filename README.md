# core-reconciliation

`core-reconciliation` is the participant-side economic evidence reconciler.
It consumes immutable canonical execution, correction and bust facts from
independently sequenced primary, order-session, Drop Copy and clearing streams.

The package deliberately does not own orders, fills, positions or clearing.
It maintains source-local contiguous cursors, deduplicates by stable economic
identity, exposes uncertainty and conflicts, and publishes a common
business-time completeness watermark only while every required source is
fresh, healthy and gap-free.
