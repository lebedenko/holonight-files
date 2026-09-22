# Design

Extend `files-smoke` with an environment-gated fixture generator and three performance
exercises. Generation is a separate process using existing Qt codecs and EXIF fixture
helpers. Solid interior color regions distinguish each image and independently check
EXIF quadrant order; gradients/detail and PNG alpha surround these regions. PNG interior
samples are exact, JPEG samples allow five channel levels of loss.

A Files-local standard-library Python runner follows Viewer's measurement format without
modifying Viewer. It requires a Release binary from this checkout, captures CMake and
installed-provider evidence and hashes, generates fixtures before trials, and runs five
sequential processes per scenario with explicit offscreen software rendering. XML metrics
have exact schemas; no latency threshold enters ordinary CI. RSS is sampled from `/proc`
and peak RSS comes from `wait4`. Output directories must be empty.

Cache workload uses each source once cold, recreates the service for disk reuse, then
clears/reselects within memory capacity. Resize crosses 128/256/512/1024 tiers, activates
1800-pixel Quick Look, rapidly requests through 2300 pixels and returns to the pane.
Pressure traverses six images forward/backward for three cycles at 4096×4096, rapidly
replaces selection, then shuts down with pending work. The existing worker hook counts
attempts, including attempts cancelled before successful decode. No artificial delay
is injected. `changed` timestamps adequate content and final metadata; polling only
awaits completion. RSS checkpoints and GUI timer gaps accompany pressure samples.

The production service remains unchanged until baseline evidence demonstrates a defect.
The smoke entry point preserves caller cache isolation only for the benchmark opt-in.

## Demonstrated defect and scoped correction

The baseline rounds 6000×4000 to 2300×1533, then passes that fitted size as the
provider's bound. The provider's second fit yields 2299×1533. Adequacy checks keep
requesting upgrades, repeatedly decoding the source without ever meeting the bound.
Pass the original requested rectangle to full decoding and retain the fitted size
only for cache adequacy checks. Disk tiers, cache budgets, provider contracts,
timeout/debounce and cancellation behavior are unchanged.

A deterministic PNG/EXIF 2/6/7 regression checks exact fitted dimensions and memory
reuse for awkward integer bounds. The measured workloads use a normal Qt event loop,
with a timer only checking completion; QTest's sleep/processEvents polling otherwise
adds a ten-millisecond notification floor. Failed trials retain their evidence, all
five trials are attempted, and any failure makes the complete runner return nonzero.
