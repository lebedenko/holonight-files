#!/bin/sh
set -eu
lane=$1
mkdir /work/files
cp -a /input/. /work/files/
cd /work/files
export HOME=/work/build/home LC_ALL=C.UTF-8 TZ=UTC JOBS=2 CMAKE_BUILD_PARALLEL_LEVEL=2
mkdir -p "$HOME"
chmod 700 "$HOME"
if [ "$lane" = licensing ]; then
  reuse --version
  reuse lint
  exit
fi
export QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=software
python3 --version
clang-format --version
clang-tidy --version
reuse --version
task --version
locale -a
python3 scripts/ci/test_launcher.py
python3 scripts/ci/test_tooling_tidy.py
mkdir -p build/ci-provenance
pacman -Q > build/ci-provenance/packages.txt
c++ --version > build/ci-provenance/compiler.txt
cmake --version > build/ci-provenance/cmake.txt
qmake6 --version > build/ci-provenance/qt.txt
cp scripts/ci/packages.json build/ci-provenance/supplements.json
capture() {
  status=$?
  cp -a build/ci-provenance /output/
  if [ -d build/test/Testing ]; then cp -a build/test/Testing /output/; fi
  exit "$status"
}
trap capture 0
fetch_provider() {
  name=$1
  revision=$2
  git init -q "/work/$name"
  git -C "/work/$name" fetch --depth 1 "https://github.com/lebedenko/$name.git" "$revision"
  git -C "/work/$name" checkout --detach FETCH_HEAD
  [ "$(git -C "/work/$name" rev-parse HEAD)" = "$revision" ]
}
fetch_provider holonight-config 03fa635cedc506e101a148fc54f7eb46d17c6de5
fetch_provider holonight-qt 61d0c16af689e960367fd0f631f2c88afa169441
fetch_provider holonight-images ac11f23e9ff2d70b1c142b643bc2abb6dd69f6c1
fetch_provider holonight-thumbnails d27addc044f277686850588147ec825c40c0f252
fetch_provider holonight-system-services f93ae00fcb850426c433e4e9f97b569979f330d6
fetch_provider holonight-search d12e441c06d1df87450514136c5a2d458d985ed3
task deps
cp build/deps/provider-revisions.tsv build/ci-provenance/
export LD_LIBRARY_PATH="$PWD/build/deps/prefix/lib"
if [ "$lane" = isolation ]; then
  task configure PRESET=test
  cmake --build --preset test --target files-fsops-smoke files-fsops-window-smoke --parallel 2
  cp -a /work/files /output/files
  cp -a /work/packages /output/packages
  cp scripts/ci/packages.json /output/packages.json
  cp scripts/ci/install-runtime-tools.py /output/install-runtime-tools.py
  cp scripts/ci/isolation-test.sh /output/isolation-test.sh
else
  task check
  LC_ALL=en_US.UTF-8 task test
  bash scripts/prepare-runtime-check.sh
  context=$(cat build/runtime-check-context)
  cp scripts/ci/Dockerfile.runtime "$context/Dockerfile"
  cp -a "$context" /output/runtime-context
  cp -a /work/packages /output/runtime-context/packages
  cp scripts/ci/packages.json /output/runtime-context/packages.json
  cp scripts/ci/install-runtime-tools.py /output/runtime-context/install-runtime-tools.py
  cp scripts/check-runtime-fixtures.py /output/check-runtime-fixtures.py
fi
