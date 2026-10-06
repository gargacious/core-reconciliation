# Current work

## FMI-03 — COMPLETE LOCALLY

Extend the independent reconciliation owner across venue trade, clearing,
settlement-obligation, position, CSD, custody, settlement-bank cash and
client/broker evidence. Exact asset, currency, jurisdiction and provider
tolerance policy is required. Missing evidence, mismatches, conflicting retry,
source gaps and owner-generation drift open immutable, versioned exception
cases. Maker/checker resolution records govern the case only and never rewrite
another owner's economic fact.

## Prior task: DIST-I10 — COMPLETE

Answer C12 causal-support queries from retained participant reconciliation
source evidence without manufacturing business truth. The bounded projection
registers exact source owners, accepts only contiguous owner-scoped evidence,
fences configuration/generation changes, requires all required sources to be
fresh and healthy, filters privacy and causal fields, and supports exact
record-identity continuation.

## Earlier task: PART-02 — COMPLETE

Build the independent participant reconciliation owner for primary,
order-session, Drop Copy and clearing evidence. Preserve a separate contiguous
cursor for each registered scope, deduplicate executions/corrections/busts by
stable economic identity, retain explicit conflict records and publish a
fail-closed completeness watermark.

## Qualification boundary

Repository-local tests cover deterministic fixed-capacity FMI reconciliation,
exact tolerance selection, all eight evidence roles, missing evidence,
quantity/cash breaks, exact retry, conflicting retry, source gaps,
owner-generation fencing, maker/checker case resolution and economic-fact
non-mutation. They also retain the C12 and PART-02 cases. They do not claim a
durable database, production process, live venue/CCP/CSD/custodian/bank
connection, hosted scale or external operational certification.

## Next action

The integration owner can now compose public package revision 3 with the
installed FMI lifecycle owners and retain the FMI-03 cross-package receipt.
Durable journal/checkpoint storage, external provider operation and
visibility-policy enforcement remain deployment/owner concerns.

## Verification

- Focused FMI reconciliation test: 1/1 passed.
- Full repository suite: 5/5 passed in the one final bounded-task run.
- Installed package manifest/resource integrity and installed FMI executable:
  passed.
