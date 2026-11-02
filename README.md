# UNDMA V2

2026-11-02 目录边界：V1/V2 已拆成独立 Git 仓库，各自提交，不再共用 Git 数据库。研究材料在 `../../02_RESEARCH/un-dma-research/v2-product-research`，自测、验证和预览工具在 `../../04_SCENARIO/un-dma-v2`。产品目录不得重新添加 research、测试源码或测试构建。正式程序保留 `build/Release`；`./build-release.ps1 -Tests` 的输出位于 Scenario 的 `build/selftests`。以下旧路径和 worktree 说明仅为历史记录，以本条为准。


正式主线 `v2/framework` 已接入实验 C 的统一采集、精简热读取、分步发现、骨骼缓存和共享消费帧。采集固定 **300Hz**，使用高精度 1/300 秒周期，无双档切换入口。

先读 [`docs/V2.md`](docs/V2.md)。本目录构建 `build/Release/UNDMA_V2.exe`，启动入口 `start-v2.ps1`；V1 保留在 `../un-dma-v1`，无需切换分支或重建即可使用。

当前状态见 `docs/V2_PROGRESS.zh-CN.md`，阅读地图见 `docs/V2_CODE_MAP.zh-CN.md`。2026-10-30 正式 Release 构建通过；45 秒 CN 短跑启动被自动审批拒绝，本次现场未验。B/C 整体归档在 `../../06_ARCHIVE/un-v2-experiments-20261030/`。
