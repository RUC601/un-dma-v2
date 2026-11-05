# UNDMA V2 Agent Context

## 产品文档归属（2026-11-05）

V2 产品文档统一放在 `../docs/un-dma-v2/`，跨产品共用的文档放在 `../docs/`。V2 产品目录内禁止存放产品文档。

## 用户指定保留的直接 CD 读取研究（2026-11-05）

`include/Game/DirectCooldownResearch.hpp` 是用户明确要求长期保留、便于分享的完整注释代码档案，记录已跑通的 CN154905N 非本地技能 CD 读取实现、映射及证据。正式版采用 active 状态计时器；该档案不参与编译或运行。禁止 AI 以死代码、实验已退役、清理或精简为由自行删除、缩写或迁出。只有用户明确要求删除此研究成果时才能删除；维护工具或后续重构必须保留注释正文与溯源说明。

2026-11-02 目录边界：V1/V2 已拆成独立 Git 仓库，各自提交，不再共用 Git 数据库。研究材料在 `../../02_RESEARCH/un-dma-research/v2-product-research`，自测、验证和预览工具在 `../../04_SCENARIO/un-dma-v2`。产品目录不得重新添加 research、测试源码或测试构建。正式程序保留 `build/Release`；`./build-release.ps1 -Tests` 的输出位于 Scenario 的 `build/selftests`。以下旧路径和 worktree 说明仅为历史记录，以本条为准。


2026-10-30：当前主线已接入实验 C，统一采集固定 300Hz，无 240Hz 或频率切换入口。B/C 已归档至 ../../06_ARCHIVE/un-v2-experiments-20261030/；继续维护本目录。当前 Release 构建通过，本次 45 秒 CN 短跑启动被自动审批拒绝，现场未验；详见 ../docs/un-dma-v2/V2_PROGRESS.zh-CN.md。

本目录为独立演进工作区 `D:\Desktop\SenseZen\ECS_O\01_PRODUCTS\un-dma-v2`，分支 `v2/framework`，从 V1 `00abf5b` 继承。先读 `../docs/un-dma-v2/V2.md`、ECS_O 的 `00_META/UN_UNIFIED_REFACTOR_20261028.md` 和唯一进度 `UN_UNIFIED_PROGRESS_20261028.md`。

此前“只建立工作区和验证隔离”的迁移阶段已经结束。后续 V2 实施以 `../docs/un-dma-v2/V2_REFACTOR_PLAN.zh-CN.md` 为现行总纲、`../../02_RESEARCH/un-dma-research/v2-product-research/V2_RESEARCH_NOTES.zh-CN.md` 为研究索引，按用户在执行线程中确定的目标推进；当前已进入实现与现场核验，实际状态见 ../docs/un-dma-v2/V2_PROGRESS.zh-CN.md。本工作区不承担 V1 日常维护；V1 独立位于 `../un-dma-v1`，其分支 `v1/cn` 和现有可用程序不能被覆盖、切换或 reset。

命名 CN=国服/NetEase、KR=Nexon、BZ=普通国际服。CN 为完整业务路径，KR/BZ 仅有显式只读候选路由，继承值和门控不因 V2 名字改变。后续本轮只跑通并验证 CN；KR/BZ 按已有 Pure 证据接入可编译的候选实现并标为实服未验证，不安排 KR/BZ 实服验证，也不以其缺席阻塞收口。不把相机可用当整体适配，不编造缺失资料。

保留 UN 自有算法、Behavior 定义、参数关系和 Slot 业务语义，Pure 主要作为运行组织参考；UI 按 UN 业务重设计，使用用户新安装的 jakubkrehel/skills 套件，旧 make-interfaces-feel-better 不再是活动入口。V2 初期允许成体系重构，暂停 Ponytail 的最小改动取向。目标模式中局部未解决项不阻断独立工作；全部完成则结束，全局阻塞或仅剩必须用户介入的事项时暂停并报告，不把待验写成已通过。

最终目标是能实际运行和使用的正式 V2：CN 真实采集、显示、UN 算法/Behavior、当前设备输出和新 UI/配置形成完整链路。直接实施、构建、运行并修复；模拟、回放和离线预览仅为可选调试工具，不是交付终点，不要求先造一套模拟框架或逐阶段等待确认。Slot 新增规则已获用户确认：复制参数、清空绑定、默认停用。

构建 `./build-release.ps1`，程序 `build/Release/UNDMA_V2.exe`，入口 `./start-v2.ps1`，显式 TestServer 端口 19560。build/config/logs/产物必须工作区自有，禁止链接到 V1；不能并发竞争硬件。Scenario 仅继承现有代码，不承诺后续兼容。离线编译和配置检查不证明现场运行。

后续按模块实际需要沿用自测，不要求搬运整套旧测试框架。不得删除 V1 测试或把不可用能力标成已验收。offset/大证据归 03_DOWNP，遥测和视觉归 04_SCENARIO；Pure 等其他进行中的研究保持独立。
