# Verification — 2026-09-22

Starting Files revision: `f0a5ed66c16cc53b6d059778989d17e822a68367`, initially clean.
Only `.github/workflows/build.yml` and this local SDD change.
Provider revisions used in the separate container checkouts and verified ledger:

- config `fe69a59e6b73167fd5349223a4d265d75386c139`
- Qt `863af4183bdf09ce05199b37e8f5dfb46a311ba1`
- Images `efe3e780327fa793fb76c82b18fddde15298120b`

## Hosted evidence

Checked once using `gh run list --repo lebedenko/holonight-files --commit
f0a5ed66c16cc53b6d059778989d17e822a68367 --limit 10 --json
databaseId,headSha,status,conclusion,url,name`:

- [Build and checks](https://github.com/lebedenko/holonight-files/actions/runs/35758528862): completed/failure.
- [Licensing](https://github.com/lebedenko/holonight-files/actions/runs/35758529031): completed/success.

`gh run view 35758528862 --repo lebedenko/holonight-files --log-failed` confirmed
`fatal: detected dubious ownership in repository at '/work/holonight-config'` at
17:07:22 UTC during `task deps`, before compilation. The log retrieval needed a network
permission retry. No polling, publication or CI rerun occurred. The failed hosted revision
remains a failure; local corrected verification is separate evidence.

## Local container acceptance

Separate clones live in `/tmp/shared-hardening-container`, with Files named `files`.
The only patch copied into that Files checkout was the workflow trust loop. Its exact
`bash -euc` body was extracted to `/work/verify.sh` and checked with `bash -n`.
Existing `files-ci` image: `6b8899cffd56`; GCC 16.2.1 and Qt 6.11.2.

```sh
docker run --rm --network none -e JOBS=4 -e CMAKE_BUILD_PARALLEL_LEVEL=4 \
  -v /tmp/shared-hardening-container:/work files-ci \
  bash -euc 'id files-test; git -C /work/holonight-config rev-parse HEAD && exit 1; bash /work/verify.sh'
docker run --rm --network none -v /tmp/shared-hardening-container:/work:ro \
  files-ci bash /work/trust-probe.sh
```

The initial root revision lookup reproduced the rejection. The extracted workflow then
prepared dependencies successfully. The independent trust probe passed: root and UID 1001
both rejected all four paths before the loop and resolved their exact revisions afterward.
`git config --show-origin --get-all safe.directory` under the non-root user showed only
those four entries from `/etc/gitconfig`. Source mounts were read-only for that probe;
no host configuration or mounted source ownership was changed.

The full extracted workflow exited 0. Root `task deps` performed clean provider builds;
`files-test` then completed `LC_ALL=C.UTF-8 task check` (clean Debug/Release/test builds,
16/16 CTest, formatting, all 94 clang-tidy translation units, QML lint, licensing, staged
installation, import policy and generated QML metadata). `LC_ALL=en_US.UTF-8 task test`
also passed 16/16. Both passes include the provider-revision regression, and subsequent
preparation recognized the exact dependency revisions as current.

The smoke suite passed 555 cases with two existing native/performance opt-in skips.
The default container denies `unshare(CLONE_NEWUSER|CLONE_NEWNS)`: 16 cross-filesystem
and three cross-filesystem window cases explicitly skipped. The optional accelerated
separator matrix is off in the unmodified workflow; six software scales passed. These
are limitations of this exact CI-container acceptance, not claims of native/GPU or
cross-mount coverage. No privilege expansion or unrelated test-policy changes were made.

Full build/check logs were reviewed. No compiler or user-code clang-tidy diagnostics
remain. Existing informational notices concern Qt private-module ABI coupling, unavailable
optional Vulkan headers/theme-generator support, unused generic provider CMake options,
and suppressed system-header/NOLINT diagnostics. None arises from the trust change.

The exit trap restored `/work/files/build` to the checkout owner's `1000:1000`; the source
checkout ownership was unchanged. Runtime staging completed as `files-test`.

```sh
docker build -t holonight-files-hardening-runtime \
  /tmp/shared-hardening-container/files/build/runtime-check.fOrKP9
docker run --rm --network none holonight-files-hardening-runtime
```

Both passed. Runtime image `36a97975cfeb4dd7eef5d11569378dc8df3156cdb41e954d175596153abfd0a4`
uses the same CI image/Qt ABI, installed payloads only, no workspace mount and the
unprivileged test user. Payload ownership/modes, runtime paths, directory association
and the three-second desktop launch observation passed. Full runtime logs reviewed.

Final local documentation licensing passed with `reuse --no-multiprocessing lint`
(316/316 files). Shell syntax, the exact workflow/body comparison, local SDD links and
`git diff --check` passed. No second application build is needed for the final SDD text.

Evidence logs: `/tmp/shared-hardening-files-{ci,container,trust,runtime-build,runtime}.log`. The original
initiative and historical orientation evidence remain unchanged; publication and umbrella
pin updates are outside scope.
