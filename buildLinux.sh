#! /bin/sh

#call: buildLinux.sh distributionName version
# e.g. buildLinux.sh CentOS7 4.0.0.49

git submodule update --init --recursive

cmake -BBuild/Release/x64/CVODE/ -Hsrc/CVODES/ -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE -DCMAKE_BUILD_TYPE=Release -DBUILD_ARKODE=OFF -DBUILD_CVODE=OFF -DBUILD_CVODES=ON -DBUILD_IDA=OFF -DBUILD_IDAS=OFF -DBUILD_KINSOL=OFF -DBUILD_SHARED_LIBS=OFF -DEXAMPLES_ENABLE_C=OFF -DBUILD_STATIC_LIBS=ON
make -C Build/Release/x64/CVODE/
cmake -BBuild/Release/x64/ -Hsrc/OSPSuite.SimModelSolver_CVODES/ -DCMAKE_BUILD_TYPE=Release -DlibCVODES=Build/Release/x64/CVODE/src/cvodes/libsundials_cvodes.a
make -C Build/Release/x64/

cmake -BBuild/Debug/x64/CVODE/ -Hsrc/CVODES/ -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE -DCMAKE_BUILD_TYPE=Debug -DBUILD_ARKODE=OFF -DBUILD_CVODE=OFF -DBUILD_CVODES=ON -DBUILD_IDA=OFF -DBUILD_IDAS=OFF -DBUILD_KINSOL=OFF -DBUILD_SHARED_LIBS=OFF -DEXAMPLES_ENABLE_C=OFF -DBUILD_STATIC_LIBS=ON
make -C Build/Debug/x64/CVODE/
cmake -BBuild/Debug/x64/ -Hsrc/OSPSuite.SimModelSolver_CVODES/ -DCMAKE_BUILD_TYPE=Debug -DlibCVODES=Build/Debug/x64/CVODE/src/cvodes/libsundials_cvodes.a
make -C Build/Debug/x64/

nuget pack src/OSPSuite.SimModelSolver_CVODES/OSPSuite.SimModelSolver_CVODES_$1.nuspec -version $2
