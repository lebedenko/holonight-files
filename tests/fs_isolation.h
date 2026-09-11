#pragma once

#include <QString>

// Cross-filesystem-boundary test fixture (SPEC.md's Verification & Testing Notes: "two distinct
// filesystems... to trigger EXDEV" and per-partition trash directory selection). Rather than
// requiring root or a real loopback device, this creates a fresh, unprivileged user+mount
// namespace (unshare(CLONE_NEWUSER|CLONE_NEWNS), the same mechanism bubblewrap/podman use) so
// every tmpfs mounted afterward is a genuinely distinct st_dev from the process's original
// filesystem and from every other such mount — real stat()/rename() behavior exercises
// REQ-F-036/041 boundary detection and REQ-F-042/043 directory selection, never a mocked
// filesystem (REQ-C-002's spirit). A size-limited mount (`size=` option) makes writing past its
// capacity reliably yield ENOSPC, covering REQ-F-033/035's disk-full scenario without root or
// real quotas.
//
// This must run in its own test binary (files-fsops-smoke), never inside files-smoke: becoming
// "root" inside the new user namespace grants CAP_DAC_OVERRIDE over anything the namespace itself
// mounts, which would silently defeat files-smoke's chmod-000 permission-denied fixtures if the
// two shared a process.
namespace fs_isolation {

struct SetupResult {
  bool available = false;  // false if the sandbox disallows unprivileged user namespaces
  QString unavailableReason;
};

// Must be called exactly once, as the first thing in main(), before any threads exist (unshare()
// requires a single-threaded process) and before any Qt object is constructed.
SetupResult setUp();

// Mounts a fresh, uniquely named tmpfs and returns its path (empty on failure). `options` is
// passed verbatim as mount()'s data argument (e.g. "size=131072" for a capacity-limited
// instance). Safe to call repeatedly, any time after a successful setUp() — unlike setUp()
// itself, this has no single-threaded requirement.
QString mountFreshTmpfs(const char* options = nullptr);

}  // namespace fs_isolation
