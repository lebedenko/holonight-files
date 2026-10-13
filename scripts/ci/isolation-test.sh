#!/bin/sh
set -eu
# Offline root setup; only the namespace tests receive relaxed seccomp.
cp -a /evidence/files /work/files
chown -R "$CI_UID:$CI_GID" /work/files
mkdir -p /work/build/home
chown "$CI_UID:$CI_GID" /work/build/home
chmod 700 /work/build/home
cd /work/files
set +e
setpriv --reuid="$CI_UID" --regid="$CI_GID" --clear-groups \
  env HOME=/work/build/home LC_ALL=C.UTF-8 QT_QPA_PLATFORM=offscreen \
  QSG_RHI_BACKEND=software LD_LIBRARY_PATH=/work/files/build/deps/prefix/lib \
  FILES_REQUIRE_FS_ISOLATION=1 ctest --preset test -R '^files-fsops' -V
status=$?
cp -a build/test/Testing /output/isolation-testing
chown -R "$CI_UID:$CI_GID" /output/isolation-testing
exit "$status"
