# Finder ranking quality

Status: Superseded by [shared search adoption](../shared-search-adoption/README.md).

The UI prototype treated the whole query as one ordered subsequence, causing `lambda audit` to miss intended paths. The shared engine now owns unordered AND terms, smart case, boundary and consecutive scoring, and filename preference. Files owns traversal and presentation. This separate Files-only cycle has no active tasks.
