# Qt 与 FreeRTOS 设备监控仿真系统

Qt 上位机展示温度、电压、状态及曲线；基于 FreeRTOS Windows 移植层的模拟设备通过本机 TCP 与上位机通信，支持故障模拟与恢复。

本项目使用软件模拟数据，未在 STM32 或真实串口完成端到端验证。

## 构建

设备端使用 Visual Studio 打开 `FreeRTOS/DeviceController/controller.vcxproj`，选择 Release / Win32 构建。

上位机需要 Qt 5（Widgets、Network、SerialPort），在 Qt 开发环境进入 `device_monitor` 后执行 `qmake device_monitor.pro` 并用对应工具构建。分别运行构建出的程序，在上位机选择“FreeRTOS 模拟设备”并连接。

详细协议、测试与使用说明见 `FreeRTOS/DeviceController/README.md` 和 `device_monitor/README.md`。源码包不包含预编译程序和 Qt 运行库。

## 第三方组件

`FreeRTOS/FreeRTOS/Source` 包含 FreeRTOS-Kernel 的本机固定版本，许可证见该目录下的 LICENSE。
