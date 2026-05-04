# Arduino 原生草图目录

这个目录用于解决主程序编译时必须复制到临时目录的问题。

Arduino IDE / Arduino CLI 要求：草图目录名必须和 `.ino` 文件名一致。这里的目录是：

```text
arduino/smartdooreye/
  smartdooreye.ino
```

因此以后可以直接编译这个目录，不再把 `主程序` 复制到 `C:\tmp` 临时草图目录。这样做的好处是：

1. Arduino 可以复用同一个草图路径下的增量编译结果。
2. 报错路径稳定，学生更容易从日志定位文件。
3. 原来的 `主程序` 目录保持不变，方便对照旧结构。

推荐命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\compile_main_native.ps1
```

脚本会使用固定的本地 build 目录：

```text
.arduino-build/smartdooreye
```

这个目录只放编译产物，不提交到 Git。

## 本机测速

在本机用同一个草图目录和同一个 `.arduino-build/smartdooreye` 编译目录测试：

```text
首次固定目录编译：约 240 秒
第二次不改源码重编译：约 35 秒
第三次去掉过时 cache 参数后重编译：约 35 秒
```

结论：ESP32 Arduino 完整编译仍然比较重，但固定草图路径后，重复编译不再需要复制临时目录，增量编译可以稳定下降到几十秒级。
