.pragma library

// Candidate chains the image://icon/ provider has already failed to resolve in this engine.
// Qt Quick does not cache failed image-provider results, so without this every row sharing an
// unresolvable chain would re-request it and log its own "Failed to get image" warning
// (SPEC.md REQ-F-022: at most one message per distinct icon name, never one per row). Only a
// restart picks up an icon theme installed while the application is running.
const unresolved = new Set();

function isUnresolved(chain) {
    return unresolved.has(chain);
}

function markUnresolved(chain) {
    unresolved.add(chain);
}
