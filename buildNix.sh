#! /bin/sh

#call: buildLinux.sh distributionName version
# e.g. buildLinux.sh ${1} 4.0.0.49

nuget sources add -name sundials_cvodes -source https://ci.appveyor.com/nuget/cvodes
nuget install packages.config -OutputDirectory packages -ExcludeVersion

git submodule update --init --recursive

cmake -BBuild/Release/x64/ -Hsrc/OSPSuite.SimModelSolver_CVODES/ -DCMAKE_BUILD_TYPE=Release -DlibCVODES=packages/CVODES.${1}/CVODES/bin/native/x64/Release/libsundials_cvodes.a
make -C Build/Release/x64/

cmake -BBuild/Debug/x64/ -Hsrc/OSPSuite.SimModelSolver_CVODES/ -DCMAKE_BUILD_TYPE=Debug -DlibCVODES=packages/CVODES.${1}/CVODES/bin/native/x64/Debug/libsundials_cvodes.a
make -C Build/Debug/x64/

dotnet pack src/OSPSuite.SimModelSolver_CVODES/pack.csproj -p:PackageVersion=$2 -o ./
