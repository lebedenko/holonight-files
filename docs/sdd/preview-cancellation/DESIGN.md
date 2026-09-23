# Design

Descriptor helpers require a const atomic cancellation reference. Path convenience
wrappers retain synchronous behavior with a local false token. Every provider call
uses the request token. Cancelled outcomes return empty images without errors;
boundary checks prevent fallback and discard results observed after cancellation.

A small internal stage callback travels per request through the thumbnail helpers.
PreviewService snapshots it when starting a worker job, using its existing friend
test-access pattern. Tests synchronize at cache inspection/decode, original decode
completion and the final boundary after PNG encoding and before QSaveFile commit.
There is no global hook or QML-facing API. Empty callbacks are the production default.

Tests use generated images, isolated caches, bounded semaphore waits and scope-guard
release before service destruction. Production keeps generation and timeout guards.
