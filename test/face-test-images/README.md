# Face Test Images

这个目录保存远程调试用的小 JPEG 真人/肖像测试图。它们用于 ESP32-CAM 暂时不可用时，测试主控的完整图片链路：

1. 从 GitHub raw URL 下载 JPEG。
2. 检查 JPEG 头尾标记。
3. 转成 Base64。
4. 上传百度云 AI 人脸接口。
5. 默认阻止注册，避免把公开测试图写进真实访客库。

这些图都压成接近 ESP32-CAM QVGA 抓拍的大小，避免原图过大导致 XIAO ESP32C6 内存压力失真。

| File | Size | Image size | GitHub raw URL |
| --- | ---: | ---: | --- |
| `cornelius_250.jpg` | 26,427 bytes | 250 x 317 | `https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/face-test-images/cornelius_250.jpg` |
| `cleveland_220.jpg` | 18,812 bytes | 250 x 333 | `https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/face-test-images/cleveland_220.jpg` |
| `john_dewey_220.jpg` | 10,847 bytes | 250 x 325 | `https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/face-test-images/john_dewey_220.jpg` |
| `small_girl_watts_220.jpg` | 21,554 bytes | 250 x 313 | `https://raw.githubusercontent.com/zhangtianhai2017/Xuanxuan/main/test/face-test-images/small_girl_watts_220.jpg` |

Sources:

- `cornelius_250.jpg`: Wikimedia Commons, `File:1839 Self-portrait by Robert Cornelius (cropped).jpg`, public domain.
- `cleveland_220.jpg`: Wikimedia Commons, `File:Stephen Grover Cleveland Portrait (3x4 close cropped).jpg`, public domain / Library of Congress source.
- `john_dewey_220.jpg`: Wikimedia Commons, `File:Portrait of John Dewey, young (cropped).jpg`, public domain in the United States.
- `small_girl_watts_220.jpg`: Wikimedia Commons, `File:George Frederic Watts (1817-1904) - Portrait of a Small Girl with Fair Hair and Full Face - COMWG 104 - Watts Gallery.jpg`, public domain artwork reproduction.
