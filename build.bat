mkdir build
cd build
cmake -G "Visual Studio 16 2019" -DOpenCV_DIR="C:\opencv\build" ..
cmake --build .  -j --config "Release"
cd ../