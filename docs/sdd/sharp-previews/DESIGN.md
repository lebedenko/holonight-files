# Design

Approved by the supplied implementation plan; routine API details follow.
ThumbnailService exposes Tier, native-capped requiredSize, tierForSize, tier-aware
lookup and lookupOrDecode, and keeps bounded direct decode. Lookup validates PNG
headers before allocation and metadata plus actual dimensions after read. Cache
writes use QSaveFile and owner-only permissions; only the chosen directory is created.
PreviewService reads dimensions through its verified descriptor, queries memory
by identity and adequate image dimensions, then disk before original decode.
One display image is published before EXIF; final metadata carries no new pixels.
The existing consumer physical-pixel API and generation/cancellation queue stay.
Resize compares native-capped requirements with retained/dispatched sizes.
Memory upgrades replace the same identity entry; LRU retains two entries/64 MiB.
