git clone https://github.com/microsoft/vcpkg
cd vcpkg
git checkout 4e9d79e36dac3b2115f14d663cc47dff3c8e58d5
call bootstrap-vcpkg.bat -disableMetrics
.\vcpkg install protobuf protobuf:x64-windows
cd ../