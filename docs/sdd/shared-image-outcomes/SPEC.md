# Shared image outcomes

Approved scope: user implementation plan. Baseline: `67d8dd9710cf13510374d23f68ce3301b8793cf6`.

- R1: When raster decoding completes, preserve the exact provider outcome until presentation.
- R2: When decoding fails, distinguish unsupported, damaged, resource-limited and unreadable images using translated messages; cancellation remains silent.
- R3: When metadata is read, retain its exact outcome and available facts. When not attempted, retain no outcome. Successful pixels remain successful when metadata fails.
- R4: Preserve cache policy, orientation, bounds, limits, scheduling, consumer-owned failures and existing public presentation APIs (Files appends error categories).

No provider, codec, format coverage, logging or performance changes. Automated verification only; native sharp-preview T5 and mixed-monitor gates remain deferred. The subsequent user request authorizes committing and pinning this work; consumer publication is required before pinning.
