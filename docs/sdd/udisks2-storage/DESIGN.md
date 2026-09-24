# Design

A consumer-owned C++ presentation model observes the shared controller models and busy/completion signals.
Rows carry stable opaque target IDs, drive grouping, translated names/states and capability flags. Consumer
filters use normalized mount locations, partition purpose and block/drive facts. All hidden provider volumes
remain in the controller topology for safe removal. Confirmation captures the provider's complete scope.

Shell registers a StorageService singleton and adds a topbar widget plus a scrollable status-popup component.
Files exposes a separate DevicesModel via DirectoryController and a separate sidebar section. Navigation intent
invalidates pending activation; current filesystem association is tracked from the complete mount topology.
Recovery enters the normal navigation reset path to invalidate directory scans and preview generations.

Focused tests use the injectable backend and test observable filtering, operation dispatch and navigation outcomes.
Required repository checks and manual hardware/compositor checks are recorded separately.
