#! /bin/sh

# Build the native library on Linux or macOS and stage it for the unified
# multi-RID NuGet package.
#
# Usage: buildNix.sh
#
# Packing the unified multi-RID NuGet package is done in the dedicated CI
# pack-and-publish job, not here, because it requires natives from all three
# platforms to be present under runtimes/.
#
# The OSP-GitHub-Packages NuGet source must be configured before running this
# script (CI does this; for local runs add it via `nuget sources add` with a
# personal access token that has read:packages scope).

set -e

UNAME_S="$(uname)"
UNAME_M="$(uname -m)"

# OSP supports macOS Arm64 and Linux x86_64. Intel macOS and Linux Arm64 are
# explicitly out of scope — fail fast so a misconfigured local run doesn't
# silently stage a wrong-arch binary under the wrong runtime folder.
if [ "$UNAME_S" = 'Darwin' ]; then
  if [ "$UNAME_M" != 'arm64' ]; then
    echo "Unsupported macOS architecture: $UNAME_M (only arm64 is supported; Intel macOS was dropped in #37)" >&2
    exit 1
  fi
  ARCH=Arm64
  RID=osx-arm64
  NATIVE_FILE=libOSPSuite.SimModelSolver_CVODES.dylib
elif [ "$UNAME_M" = 'x86_64' ]; then
  ARCH=x64
  RID=linux-x64
  NATIVE_FILE=libOSPSuite.SimModelSolver_CVODES.so
else
  echo "Unsupported architecture: $UNAME_S/$UNAME_M" >&2
  exit 1
fi

nuget install packages.config -OutputDirectory packages -ExcludeVersion

git submodule update --init --recursive

# Build native (Release only — the runtimes/<rid>/native/ NuGet convention has
# no Debug/Release axis; consumers needing native debugging build from the
# source shipped in the package under OSPSuite.SimModelSolver_CVODES/src/ and
# include/).
cmake -BBuild/Release/$ARCH/ -Hsrc/OSPSuite.SimModelSolver_CVODES/ -DCMAKE_BUILD_TYPE=Release -DRID=$RID -DlibCVODES=packages/CVODES/runtimes/$RID/native/libsundials_cvodes.a
make -C Build/Release/$ARCH/

# Stage the native binary at runtimes/<rid>/native/ — the canonical location
# read by the unified nuspec when packing.
mkdir -p runtimes/$RID/native
cp Build/Release/$ARCH/$NATIVE_FILE runtimes/$RID/native/
