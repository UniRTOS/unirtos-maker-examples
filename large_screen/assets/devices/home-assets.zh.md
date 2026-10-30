# 首页素材与 LVGL 拆分

源图：`home_reference.png`，2562 × 1440。保留原有农业素材及 `reference.png`，新增资源统一使用 `home_` 前缀。首页现已接入启动流程，实现与数据接口见 [首页接入说明](../../docs/home-integration.zh.md)。

## 透明 PNG

| 功能 | 文件 | 尺寸 |
| --- | --- | --- |
| 农业入口 | [home_agriculture.png](home_agriculture.png) | 32 × 32 |
| 工业入口 | [home_industry.png](home_industry.png) | 32 × 32 |
| 家居入口 | [home_house.png](home_house.png) | 32 × 32 |
| 农业麦穗装饰 | [home_wheat.png](home_wheat.png) | 64 × 80 |
| 新风控制 | [home_fresh_air.png](home_fresh_air.png) | 32 × 32 |
| 空调控制 | [home_air_conditioner.png](home_air_conditioner.png) | 32 × 32 |
| 地暖控制 | [home_floor_heating.png](home_floor_heating.png) | 32 × 32 |
| 设置 | [home_settings.png](home_settings.png) | 32 × 32 |
| 用户 | [home_user.png](home_user.png) | 32 × 32 |

均为 RGBA PNG，四周留透明边缘。它们从合成参考图估算背景并提取，不是原始矢量资源，细线与半透明边缘存在近似。文件名按原图中的功能位置命名，保留原图符号外观。

## 原生控件

| 原图元素 | LVGL v8 实现 | 动态与交互 |
| --- | --- | --- |
| 时钟、日期、标题、单位 | `lv_label` + 字体样式 | 数据变化时更新文本，不切文字图片 |
| 三个业务卡片 | `lv_obj` + 渐变、圆角、边框 | 卡片入口可点击，设备控制独立处理事件 |
| 土壤湿度、大棚温度、运行数量、环境数值 | `lv_label` | 独立更新，避免整屏重建 |
| 农业、工业曲线与网格 | `lv_chart` | 按采样周期更新序列，不使用静态曲线图片 |
| 灌溉、生产状态、告警 | `lv_label` + 原生状态点 | 按设备实际状态更新文字和颜色 |
| 新风、空调、地暖按钮 | 圆形 `lv_btn` + 图标 + 独立标签 | 用 `LV_OBJ_FLAG_CHECKABLE` 和 `LV_STATE_CHECKED` 表达开关状态；命令失败时恢复状态 |
| 设置、用户按钮底座 | 圆形 `lv_btn` + 图标 | 处理点击事件，弹窗或页面切换 |
| 麦穗装饰 | `lv_img` | 仅装饰，不参与点击 |

不切整张卡片、圆形按钮底座、阴影或页面背景，避免把动态数值、状态和触摸区域固化为位图。卡片内有独立按钮时，应明确事件归属，避免点击设备按钮同时触发页面跳转。

## 嵌入式资源建议

- 九张 PNG 文件合计 13,894 字节，约 13.6 KiB。这是磁盘压缩大小，不等于 Flash 中 C 数组或解码 RAM 的占用。
- 八张浅色功能图标适合转成 `LV_IMG_CF_ALPHA_8BIT`，使用 `lv_img` 的 `img_recolor` / `img_recolor_opa` 设置正常、禁用、选中颜色。纯像素数据合计 8,192 字节；4 位 alpha 可进一步降至 4,096 字节，但要检查细线效果。
- 麦穗需要保留颜色，可用 RGB565 + alpha8，64 × 80 × 3 = 15,360 字节。
- 八张 alpha8 图标加彩色麦穗共 23,552 字节，约 23 KiB，不含描述符与对齐；全部使用 RGB565 + alpha8 则是 39,936 字节，约 39 KiB。
- 当前目标为 480 × 320。原图布局需要重新排版，不能按源图像素直接照搬；最终确定图标显示为 16–24 像素时，建议离线缩小并重新导出，避免运行时缩放与多套重复资源。
- 固件已使用静态 `const` LVGL 图片描述符，不引入 PNG 运行时解码器。实际页面离线缩小到八张 20 × 20 alpha8 图标和一张 36 × 45 RGB565 + alpha 麦穗，像素数组合计 8,060 字节；原 PNG 保持不变。
- 图标不包含中文标题；中文需使用覆盖实际字符的子集字体。所有控件和数据更新应在 LVGL 任务中执行。

## 重新生成与检查

在项目根目录运行：

```powershell
& .\tools\extract_home_assets.ps1
```

脚本检查源图尺寸、输出尺寸、非空内容和完全透明的外边缘，同时将深浅底素材预览写入系统临时目录下的 `lvgl-home-assets-preview.png`。该预览仅用于素材检查，不代表完整首页或硬件显示效果。