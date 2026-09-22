# Preview performance requirements

Approved scope: implement the supplied plan from Files revision
`84d4023818978aa3d8dd72a1276d6fcee75cc3eb`, leaving all changes local and uncommitted.

- REQ-1: When opted in, Files shall measure cache reuse, resize/Quick Look and selection pressure in five sequential fresh Release processes per scenario.
- REQ-2: Before measurement, tooling shall generate six distinguishable 6000×4000 PNG/JPEG fixtures with deterministic detail, PNG transparency and JPEG EXIF 2/6/7, and record encoded hashes.
- REQ-3: Each accepted selection shall validate identity, oriented dimensions, adequate pixels and image content. Signals shall timestamp adequate pixels and metadata completion separately from source-decode attempts.
- REQ-4: Tooling shall isolate XDG paths per trial and preserve XML, logs, raw metrics, sampled/peak RSS, summaries and build/provider/instrumentation provenance. Failed, skipped, malformed or incomplete trials shall fail acceptance.
- REQ-5: Measurements shall preserve production timeout, debounce, allocation, descriptor, cancellation and stale-result behavior. Only repeatably demonstrated Files-local bottlenecks justify production fixes, with deterministic regressions and identical baseline/candidate trials.
- REQ-6: Acceptance shall include focused regressions, runner failure paths, a clean Release build, the matrix and `task check`. Installed-runtime acceptance applies if production or installed payloads change.

No shared framework, runtime dependency, public API/QML/provider changes, publication,
umbrella pinning, host cache flushing or private images. Empty Files caches do not mean
cold OS storage. Generated images and offscreen software rendering do not qualify
native sharpness, physical scaling or compositor behavior. Historical native acceptance
and the open sharp-preview native task remain unchanged.
