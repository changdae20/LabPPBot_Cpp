git clone https://github.com/microsoft/vcpkg
cd vcpkg
git reset --hard f6a5d4e8eb7476b8d7fc12a56dff300c1c986131
call bootstrap-vcpkg.bat -disableMetrics
.\vcpkg install protobuf protobuf:x64-windows fmt fmt:x64-windows
cd ../