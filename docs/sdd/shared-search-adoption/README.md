# Shared search adoption in Files

Status: Implementation in progress. Provider `d12e441` is published and pinned in the umbrella.

The [umbrella initiative](../../../../docs/initiatives/shared-search-engine/README.md) owns coordination. Files owns traversal, hidden-path policy, worker scheduling, result navigation, and the existing QML popup.

## Requirements

- The finder shall stream source-owned path records to `HolonightSearch::Index` with stable path IDs, filename and relative-path fields, and explicit weights.
- Space-separated query terms shall match in any order; `lambda`, `lambda func`, `lambda audit`, and `audit lambda` shall find and rank intended paths.
- The path profile shall prefer filename matches while allowing terms across filename and directory fields.
- Directory and file finder modes shall filter source records independently.
- Hidden paths shall be excluded by default with an explicit include-hidden option; symlinked directories shall not be traversed.
- An obsolete query or scan shall not publish results. Partial matches shall appear while scanning. Existing popup, shortcuts, roots, and navigation shall remain.
- On the user machine, two-million-path warm-index final-keystroke to stable results p95 shall be under 100 ms. Cold scan progress, first result latency, memory, and index size shall be recorded separately.

## Design

`PathScanner` remains the only filesystem traversal layer. `PathFinderModel` owns one session index per root and hidden-path setting and pushes scanned batches into it. It requests capped results from a worker thread and maps returned stable path IDs to navigation data. The source is split by path kind, so directory and file mode share a completed scan without ranking the other kind. Each completed snapshot is reused for the session; an interrupted or unavailable scan is retried when reopened. A root or hidden-policy change cancels obsolete work. `PathFinderPopup.qml` keeps its interaction and layout, adding only an include-hidden control.

## Tasks

| ID | Task | State |
|---|---|---|
| F1 | Record prototype failure and supersede Files-only follow-ups | Done |
| F2 | Adopt published provider, stream batches, replace ranking | Done |
| F3 | Hidden-path option, cancellation, focused regression tests | Done |
| F4 | Required local checks and native finder acceptance | In Progress |

See [verification](VERIFICATION.md) for measured results and remaining checks. The finder is not accepted until the local gates and user-performed native check pass.
