# Design

PreviewService assigns generic metadata and initial busy state before notifying observers. A selectionChanged signal precedes changed for each accepted target revision and clear; unchanged targets and viewport upgrades do not emit it. Existing dispatch handles asynchronous notifications and preserves current-line notification ordering.

PreviewPane owns a single-shot 150 ms timer and selection-local elapsed flag. The shared fallback predicate requires an entry, no image, and either completion, an error, or an elapsed delay. All three fallback tiers use it. Thumbnail visibility remains independent of busy. Component initialization also starts the delay if attached during loading. The square image area and icon resolution chain remain unchanged.

No worker algorithms, settings, caching, decoding, cancellation or Quick Look presentation changes. Existing regression seams control worker progress; offscreen QML tests inspect actual presentation visibility.
