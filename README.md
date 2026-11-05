# UNDMA V2

2026-11-02 目录边界：V1/V2 已拆成独立 Git 仓库，各自提交，不再共用 Git 数据库。研究材料在 `../../02_RESEARCH/un-dma-research/v2-product-research`，自测、验证和预览工具在 `../../04_SCENARIO/un-dma-v2`。产品目录不得重新添加 research、测试源码或测试构建。正式程序保留 `build/Release`；`./build-release.ps1 -Tests` 的输出位于 Scenario 的 `build/selftests`。以下旧路径和 worktree 说明仅为历史记录，以本条为准。


正式主线 `v2/framework` 已接入实验 C 的统一采集、精简热读取、分步发现、骨骼缓存和共享消费帧。采集固定 **300Hz**，使用高精度 1/300 秒周期，无双档切换入口。

开发先读 [V2 开发手册](../docs/un-dma-v2/V2_DEVELOPMENT_GUIDE.zh-CN.md)，再按 [代码阅读地图](../docs/un-dma-v2/V2_CODE_MAP.zh-CN.md) 进入模块。此前 UN 开发文档保留为 V1/继承历史；V2 文档独立维护。[工作区与历史说明](../docs/un-dma-v2/V2.md) 保留迁移记录。本目录构建 `build/Release/UNDMA_V2.exe`，启动入口 `start-v2.ps1`；V1 保留在 `../un-dma-v1`。

当前状态按日期见 [实施进度](../docs/un-dma-v2/V2_PROGRESS.zh-CN.md)。2026-11-03 只读审查确认配置加载覆盖风险及回归入口失配；本轮 43 项离线检查运行、42 通过、1 失败，另 7 项因 4 个目标编译失败未运行。调用链、精简建议和覆盖范围见 [审查报告](../../04_SCENARIO/un-dma-v2/reports/readonly-audit-20261103/REVIEW.zh-CN.md)。本轮未改产品代码、正式配置或程序，未做正常实连。B/C 整体归档在 `../../06_ARCHIVE/un-v2-experiments-20261030/`。
