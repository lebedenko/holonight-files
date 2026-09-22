# Preview responsiveness and cache efficiency

Files-local measurement cycle; unchanged production baseline:
`84d4023818978aa3d8dd72a1276d6fcee75cc3eb`.

- [Requirements](SPEC.md)
- [Design and workloads](DESIGN.md)
- [Tasks](TASKS.md)
- [Findings and verification](VERIFICATION.md)

From the Files repository, prepare providers and build a separate Release tree:

```sh
task deps
cmake -S . -B build/preview-performance -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_PREFIX_PATH="$PWD/build/deps/prefix" \
  -DQML_IMPORT_PATH="$PWD/build/deps/prefix/lib/qt6/qml"
cmake --build build/preview-performance --parallel 6
python3 scripts/check-measure-preview.py
python3 scripts/measure-preview.py build/preview-performance/tests/files-smoke \
  build/preview-baseline --prefix build/deps/prefix --scenario all
```

Use an empty output directory. `--scenario cache`, `resize`, or `pressure` selects
one workload; each always runs five fresh processes. The runner checks the provider
revision ledger and build caches produced by `task deps`; a custom `--prefix` must
match those records and the application configuration. Finish builds and stop other
acceptance jobs before measurement. The runner does not flush host storage caches.

Output retains `environment.json`, `fixtures.json`, generated images, per-process
logs/XML, sampled RSS JSON, raw `results.json`, and median/min/max `summary.json`.
Per-selection `pixels_ns` timestamps the first signal with adequate, verified pixels;
`metadata_ns` timestamps final metadata with those pixels. A request already satisfied
synchronously is measured immediately after the request. `attempts` counts entry to
the existing pre-source-decode hook, not successful decodes. RSS values are KiB.

The three scenarios use real decoding, without artificial delays. Performance tests
skip by default; runner correctness runs in CTest. No timing threshold gates ordinary
CI. These fixtures do not represent photographic content or native display sharpness.
