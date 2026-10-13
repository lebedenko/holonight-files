#!/bin/sh
set -eu
lane=$1
mkdir /work/files
cp -a /input/. /work/files/
cd /work/files
export HOME=/work/build/home LC_ALL=C.UTF-8 TZ=UTC
export JOBS=${JOBS:-2} CMAKE_BUILD_PARALLEL_LEVEL=${JOBS:-2}
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
mkdir -p build/ci-provenance
pacman -Q > build/ci-provenance/packages.txt
c++ --version > build/ci-provenance/compiler.txt
cmake --version > build/ci-provenance/cmake.txt
qmake6 --version > build/ci-provenance/qt.txt
pkg-config --modversion Qt6Core > build/ci-provenance/qt-version.txt
cp scripts/ci/images.json build/ci-provenance/images.json
capture() {
  status=$?
  cp -a build/ci-provenance /output/
  if [ -d build/test/Testing ]; then cp -a build/test/Testing /output/; fi
  exit "$status"
}
trap capture 0
if [ "$lane" = prepare ]; then
  export HOLONIGHT_CONFIG_SOURCE=/prepared/sources/holonight-config
  export HOLONIGHT_SYSTEM_SERVICES_SOURCE=/prepared/sources/holonight-system-services
  export HOLONIGHT_QT_SOURCE=/prepared/sources/holonight-qt
  export HOLONIGHT_IMAGES_SOURCE=/prepared/sources/holonight-images
  export HOLONIGHT_THUMBNAILS_SOURCE=/prepared/sources/holonight-thumbnails
  export HOLONIGHT_SEARCH_SOURCE=/prepared/sources/holonight-search
  if [ -d /prepared/deps ]; then cp -a /prepared/deps build/deps; fi
  task deps PRESET=test
  python3 tooling/external.py check --module tooling/module.json --scope build --scope tool --scope test
  mkdir -p /prepared/deps
  cp -a build/deps/. /prepared/deps/
  cp -a build/ci-provenance /prepared/environment
  exit
fi
cp -a /prepared/deps build/deps
cp /prepared/provider-revisions.json build/ci-provenance/
cp build/deps/provider-revisions.tsv build/ci-provenance/
python3 scripts/ci/test_launcher.py
python3 scripts/ci/test_tooling_tidy.py
python3 scripts/ci/test_dependencies.py
export LD_LIBRARY_PATH="$PWD/build/deps/prefix/lib"
if [ "$lane" = isolation ]; then
  task configure PRESET=test
  cmake --build --preset test --target files-fsops-smoke files-fsops-window-smoke --parallel 2
  cp -a /work/files /output/files
  cp scripts/ci/isolation-test.sh /output/isolation-test.sh
else
  # Preserve the independently buildable Core and Quick components and installed consumers.
  for component in core quick; do
    quick=OFF
    if [ "$component" = quick ]; then quick=ON; fi
    cmake -S . -B "build/$component" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DBUILD_FILES_APP=OFF -DBUILD_FILE_BROWSER_QUICK="$quick" -DBUILD_TESTING=ON \
      "-DHOLONIGHT_DEPENDENCY_PREFIX=$PWD/build/deps/prefix" \
      "-DQML_IMPORT_PATH=$PWD/build/deps/prefix/lib/qt6/qml"
    cmake --build "build/$component" --parallel "$JOBS"
    ctest --test-dir "build/$component" --output-on-failure --no-tests=error
    cp -a "build/$component/Testing" "/output/$component-testing"
  done
  # The fresh full test database covers optional application and test sources once.
  export HOLONIGHT_TIDY_DATABASE="$PWD/build/test/compile_commands.json"
  task check
  LC_ALL=en_US.UTF-8 task test
  bash scripts/prepare-runtime-check.sh
  context=$(cat build/runtime-check-context)
  cp scripts/ci/Dockerfile.runtime "$context/Dockerfile"
  cp -a "$context" /output/runtime-context
  cp scripts/check-runtime-fixtures.py /output/check-runtime-fixtures.py
fi
