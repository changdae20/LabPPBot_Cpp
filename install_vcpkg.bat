git clone https://github.com/microsoft/vcpkg
cd vcpkg
call bootstrap-vcpkg.bat -disableMetrics
.\vcpkg install protobuf protobuf:x64-windows fmt fmt:x64-windows
cd ../