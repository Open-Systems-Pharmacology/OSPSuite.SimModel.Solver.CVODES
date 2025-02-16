#! /bin/sh

#call: buildLinux.sh distributionName version
# e.g. buildLinux.sh MacOS 4.0.0.49

if [ `uname -m` = 'x86_64' ]; 
then
  ARCH=x64
else
  ARCH=Arm64
fi



nuget sources add -name sundials_cvodes -source https://ci.appveyor.com/nuget/cvodes
nuget install packages.config -OutputDirectory packages -ExcludeVersion

git submodule update --init --recursive

if [ `uname` = 'Darwin' ]; 
then
  cmake -BBuild/Release/$ARCH/ -Hsrc/OSPSuite.SimModelSolver_CVODES/ -DCMAKE_BUILD_TYPE=Release -DlibCVODES=packages/CVODES.${1}$ARCH/CVODES/bin/native/$ARCH/Release/libsundials_cvodes.a
  make -C Build/Release/$ARCH/

  cmake -BBuild/Debug/$ARCH/ -Hsrc/OSPSuite.SimModelSolver_CVODES/ -DCMAKE_BUILD_TYPE=Debug -DlibCVODES=packages/CVODES.${1}$ARCH/CVODES/bin/native/$ARCH/Debug/libsundials_cvodes.a
  make -C Build/Debug/$ARCH/
else
  cmake -BBuild/Release/$ARCH/ -Hsrc/OSPSuite.SimModelSolver_CVODES/ -DCMAKE_BUILD_TYPE=Release -DlibCVODES=packages/CVODES.${1}/CVODES/bin/native/$ARCH/Release/libsundials_cvodes.a
  make -C Build/Release/$ARCH/

  cmake -BBuild/Debug/$ARCH/ -Hsrc/OSPSuite.SimModelSolver_CVODES/ -DCMAKE_BUILD_TYPE=Debug -DlibCVODES=packages/CVODES.${1}/CVODES/bin/native/$ARCH/Debug/libsundials_cvodes.a
  make -C Build/Debug/$ARCH/
fi



dotnet pack src/OSPSuite.SimModelSolver_CVODES/pack.csproj -p:PackageVersion=$2 -o ./
