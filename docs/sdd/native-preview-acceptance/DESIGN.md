# Design

`files-native-preview-lab` is a non-installed BUILD_TESTING target using
`add_files_application` and the real main.cpp. FILES_NATIVE_PREVIEW_LAB alone
includes a tests-owned observer; no production diagnostic API is added.
Read-only additions to PreviewServiceTestAccess expose identity and requests.
Signals capture publication synchronously; queued geometry observations and a
10ms heartbeat capture settled bindings, GUI gaps and atomic frame/decode counts.
Logs are JSONL, buffered by QFile; snapshots do not copy or hash image pixels.
The observer's clock starts on entry to main, before QGuiApplication construction.

The Python runner creates a never-reused pair directory. Cold and disk processes
share only that pair's cache; config/data/state are fresh for each process.
A sentinel directory is selected at startup. The operator selects Colosseum,
then Fronalpstock; the warm process repeats those selections followed by alternating
memory selections. The parser requires this exact sequence and fixed pane bounds.
All raw evidence and failures survive; aggregation requires all five pairs.
Visual sessions are separate and retain unrestricted navigation/resize evidence.
Manual annotations remain mandatory for orientation, detail and compositor output.

For the approved monitor constraint, the runner additionally permits actual 1.6×.
Report `--scales` selects four distinct supported required scales and records them
in the result; its default preserves the historical 1/1.25/1.5/2 matrix. Evidence
with 1.6× cannot satisfy a requested 1.5× row. Measurement calculations are unchanged.
