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

if [ "$(uname -m)" = 'x86_64' ]; then
  ARCH=x64
else
  ARCH=Arm64
fi

if [ "$(uname)" = 'Darwin' ]; then
  RID=osx-arm64
  NATIVE_FILE=libOSPSuite.SimModelSolver_CVODES.dylib
else
  RID=linux-x64
  NATIVE_FILE=libOSPSuite.SimModelSolver_CVODES.so
fi

nuget install packages.config -OutputDirectory packages -ExcludeVersion

git submodule update --init --recursive

# Build native (Release only — the runtimes/<rid>/native/ NuGet convention has
# no Debug/Release axis; consumers needing native debugging build from the
# source shipped in the package under OSPSuite.SimModelSolver_CVODES/src/ and
# include/).
cmake -BBuild/Release/$ARCH/ -Hsrc/OSPSuite.SimModelSolver_CVODES/ -DCMAKE_BUILD_TYPE=Release -DlibCVODES=packages/CVODES/runtimes/$RID/native/libsundials_cvodes.a
make -C Build/Release/$ARCH/

# Stage the native binary at runtimes/<rid>/native/ — the canonical location
# read by the unified nuspec when packing.
mkdir -p runtimes/$RID/native
cp Build/Release/$ARCH/$NATIVE_FILE runtimes/$RID/native/
