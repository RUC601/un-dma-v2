# UNDMA V2 开发手册

更新：2026-11-03。起始审查基线：`8f7a5e5df77b3f36190dbc241e5c8b420a17e3f4`，分支 `v2/framework`。当前包含本日 F1–F3、C1–C3 未提交修复；修复与验证记录见文末。

本手册只维护 V2。此前 UN 开发文档归 V1/继承历史，不作为当前 V2 的实现说明；不移动或改写 V1 材料。实现以本仓库代码为准，验证以对应版本、日期的证据为准。

## 目录与维护责任

| 范围 | 当前位置（相对 ECS_O） | 责任 |
|---|---|---|
| V2 产品 | `01_PRODUCTS/un-dma-v2` | 产品源码、资源、构建入口、开发文档；独立 Git 仓库 |
| V1 产品 | `01_PRODUCTS/un-dma-v1` | 独立维护 CN V1，不是 V2 的共享运行依赖 |
| V2 测试与证据 | `04_SCENARIO/un-dma-v2` | `src` 测试、`cmake/SelfTests.cmake` 注册、`build/selftests` 产物、`reports` 记录 |
| V2 研究 | `02_RESEARCH/un-dma-research/v2-product-research` | 资料、协议来源、历史和取舍，不持有产品运行代码 |
| 静态样本与偏移证据 | `03_DOWNP` | dumps、分析、偏移交接；`vertifytool` 持有只读实测工具 |
| 实验 B/C | `06_ARCHIVE/un-v2-experiments-20261030` | 冻结历史；C 的选定实现已进入 V2 |

根目录为 `D:\Desktop\SenseZen\ECS_O`。V1/V2 已于 2026-11-02 拆为独立仓库，旧说明中的共享 worktree/Git 数据库仅是历史。不得把 ECS_O 建为总仓库，也不得连接两版的活动配置、build 或 logs。

阅读顺序：本手册 → [代码阅读地图](V2_CODE_MAP.zh-CN.md) → 对应模块源码及 Scenario 测试 → [实施进度](V2_PROGRESS.zh-CN.md) 中的实际证据。[改造总纲](V2_REFACTOR_PLAN.zh-CN.md) 说明目标，[能力表](V2_CAPABILITIES.zh-CN.md) 记录来源和边界；旧日期的状态不能覆盖较新的实现。

## 完整业务流程

```mermaid
flowchart TD
    A[启动参数与独立配置] --> B[ProcessSession / 映像与 Profile 核验]
    B --> C[统一采集 worker：目标 300 Hz]
    C --> D[相机 / 分步发现 / 热读取 / 慢维护]
    D --> E[身份与新鲜度校验 / 不可变消费帧]
    E --> F[UN Behavior / 技能 / 输入所有权]
    E --> G[显示与可选插值]
    F --> H[动作调度 / 会话与设备代次门]
    H --> I[Net / Serial / CatBox / Mock]
    J[UI 编辑 / 英雄切换] --> K[配置提交与快照]
    K --> F
    K --> G
    J --> L[延迟自动保存 / 手动保存]
    B --> M[断开：停发 / 取消 / 排空 / 重新核验]
```

这是进程内模块划分。300Hz 是采集调度目标，不代表成功 DMA 读取、每个实体的慢字段、绘制或物理输出均达到 300Hz。

### 启动、会话和停止

`src/main.cpp` 解析参数。`--config-check` 和编入的自测入口在正常设备/线程启动前返回。配置检查会调用真实加载与迁移，**目前不是不写盘的校验器**，见下方已知缺陷。

正常启动加载独立配置，由 `src/runtime/ProcessSession.cpp` 创建连接线程。该线程先初始化输出，再处理 DMA 建连、健康检查、目标进程发现、映像核验及 Profile 路由。CN 是完整业务路径；KR=Nexon、BZ=普通国际服。候选需显式选择并通过匹配，只运行候选读取，不启动 UN 行为/输出。相机匹配不等于完整适配。

`WorkerGroup` 持有线程及异常状态。运行组包括统一采集、Behavior、英雄配置切换、技能运行和可选显示插值等；按键采样另有生命周期。worker 异常会进入会话失败处理。

断开/退出先关闭正常输出授权、请求停止和取消，等待调度清理及线程返回，再关闭 DMA/设备资源。`Memory` 会话门阻止新操作并等待在途访问排空。停止标志、线程返回、join 完成、资源关闭是不同事实；不能因代码有 join 就宣称 SDK 永久阻塞可取消。目标进程/DMA 恢复与外设恢复有不同入口，连接循环目前不周期性 reconcile 外设。

### 统一采集、身份和身体有效性

入口为 `src/Game/entities/EntityReader.cpp::entity_thread`，节拍在 `src/runtime/Acquisition.hpp`。每轮先等待节拍，调用 `SampleAcquisitionCamera`，再推进发现、热字段与预算内慢维护；预算不能抢占已经进入的外部调用。

- `EntityDiscovery.cpp` 保存可恢复 cursor，分步读取验证；当前 worker 固定 latest-wins/cold topology。旧环境开关不是其策略控制源。
- `RegistryResolver.hpp` 管候选根、语义核验、预算和会话变化。读完表与获得删除旧名单权限分开，部分/失败/空结果不随意获得完整删除权。
- 热读取使用批量缓冲；慢维护处理组件、身份、骨骼等。周期更新与布局/MatchId 核验用于应对组件搬家，禁止重建后继续套旧批量结果。
- 死亡字节统一为 `Health+0x5C0`，每轮热批量读取，完整 0/1 才可使用；死亡优先于位置/骨骼处理并撤销身体与目标锁。血量分项仍为 16ms，死亡未知不刷新存活依据；身体还要求死亡状态在 100ms 内。其他地区统一偏移是用户指定假设，实服未验。
- 玩家身份来自 `LinkParent` 的 `0x24` 组件和 `+0xE0` 引擎字符串对象（长度 `+0x10`，存储字段 `+0x14`，支持内联与长字符串）。身份保存在持久 owner record，当前控制器配对接替同一 BattleTag 的身体与英雄，不派生重复玩家席位；机器人保留原实体身份规则。
- 身体采用依赖当前配对、发现证明、组件版本和身份权限。2026-11-03 实现保留同一已确认配对的队伍约束，真实配对替换才重新确认，未知不默认敌方。
- 本地实体也经过空间、身份与身体接纳；消费帧基于当前已确认本地队伍计算关系。

| 状态 | 职责 | 不能替代什么 |
|---|---|---|
| connection epoch | 区分连接会话 | scene 内实体身份 |
| scene generation | 区分场景换代 | 配对/组件有效性 |
| 注册表及删除权限 revision | 防止旧扫描删除新发现 | 身体字段新鲜度 |
| actor/body authority 与配对 | 防止旧身体/旧结果复活 | 实际位置样本的时效 |
| 100ms 身体窗口 | 约束当前可消费身体 | 名单保留期 |
| 13s 席位保留 | 暂时不可见对象的身份连续性 | 允许旧身体继续动作或正常绘制 |

这些状态不能按名字相似就合并。修改时要追踪发布、撤销、迟到读取和消费端。

### 相机、快照与显示

`src/Game/camera/CameraWorker.cpp` 由统一采集调用，不再有独立高频相机线程；链由 Profile 决定，采集线程持有句柄，无效矩阵不作为有效样本发布。

`src/Game/snapshots/SnapshotAccess.hpp` 提供 `ConsumptionFrame`、`SnapshotFrame` 和 scoped 消费上下文。帧通过 `shared_ptr<const ...>` 发布，消费者持有其生命周期；矩阵和身体仍要检查新鲜度、epoch、scene。不可变保证内容不被并发改写，不保证内容永远新鲜。

持续动作循环也有刷新消费帧的路径，不能只看外层作用域就推断一直使用首帧。`PresentationWorker` 和 `features/visuals` 管插值与画面，不应以显示插值延长动作身体权限。模型形状加载成功不证明像素对齐或伤害碰撞意义。

### UN Behavior、技能和 Slot

`src/Features/aim/BehaviorController.cpp` 管激活、方案选择、目标连续性和各 Behavior；`SessionState` 保存运行状态；`Smoothing`/`Target.hpp` 提供 UN 选择和运动算法。技能位于 `HeroSkills`，通过统一动作/所有权系统输出。

Behavior 决定动作规则，Method 决定运动方式。Tracking、Flick、Flick2nd、Reacquire、Magnetic Trigger 的含义及参数关系仍属于 UN，不按其他产品分类改写。英雄局部 override 不应改变全局持久化参数。

Slot 显示下标与稳定 ID 分开：重排不变身份；新增复制参数、清绑定、默认停用并分配新 ID；删除/替换使旧运行引用失效。同一武器冷却不能靠换 Slot 清零。UI 选中方案不等于业务已激活。

## 输出所有权与恢复

| 所有者 | 入口 | 责任 |
|---|---|---|
| 业务动作 | `src/output/Actions.cpp`、量化/运动模块 | 业务意图转移动/按键/释放，不直接管理设备生命周期 |
| 调度/所有权 | `Game/OutputScheduler.cpp`、`InputOrchestrator`、`OutputOwnership` | 合并持有者、定时序列、取消与释放，共享控制不能提前释放 |
| 输出 runtime | `src/output/OutputRuntime.cpp` | descriptor、能力、应用状态、generation 与分发门；切换先清理旧会话 |
| 传输 | `KmBoxNetBackend`、`SerialTransport`、`CatBoxBackend`、`MockBackend` | 协议、队列、完成状态、物理输入监听与关闭 |
| 生命周期接线 | `DeviceConnection.cpp`、UI 设备应用、英雄配置 worker | 依据配置显式 reconcile/释放/停止 |

持久化设备编号：0 KMBox Net，1 KMBox Serial，2 Mock，3 MAKCU 4M，4 MAKCU 115200，5 Ferrum，6 CatBox。旧 `[KMBox]`/`kmbox*` 是配置兼容，公共代码在 `src/output` / `include/output`；不恢复旧转发目录。Controller/Medius 等未实现项不能冒充可用。

入队接受、写入完成、设备实际执行是不同事实。断连关闭正常分发；旧 generation 不进入新会话；安全释放不能新增按下。串口使用 `SendCommandOrFailSession`：仅发送一次，失败关闭会话并结算待处理命令，不重放失败移动。Net 重发是否由固件去重仍需设备证据。

当前 Net 在 Running 状态失去连接后，`EnsureConnected` 不重连，需 runtime 显式重建；设备应用或英雄切换可触发，网络恢复本身不会触发。历史日志“attempting reconnect”与此不符。是否恢复自动重建须先明确契约，并保留清队列/新 generation，不能仅移除门控。离线协议/Mock 测试不能证明各设备的物理效果、固件差异、时延和长跑。

## 配置：编辑、生效和落盘

运行配置在 EXE 旁的 `build/Release/config`，启动脚本将工作目录设为程序目录。系统参数为档案 INI，英雄参数为同名 `.heroes.json`，DMA 参数单独存储。模板构建不应无条件覆盖正式配置。

1. UI 使用 `ScopedConfigEdit`，修改局部视图后在锁内差异提交；Slot 命令通过 `HeroSlotStore`。worker scoped 读取来自已发布配置，英雄 override 是局部运行参数。
2. 编辑影响运行快照，落盘另行处理。读/编辑作用域仍保留所需副本；差异提交只有实际改变或引用修复时才发布。只读 getter 和空闲自动保存轮询不再主动发布；查询会先提交尚未生效的 UI 编辑，真正保存仍校验并发布，保留原有可见时点。不是零拷贝配置。
3. 自动保存默认开启：持久化内容最后变化后等待 4 秒，继续编辑重新计时，恢复到已保存值不写盘；失败保留待存内容并每 4 秒重试。手动立即保存，正常退出补存。
4. 英雄切换先保存前一英雄，失败延后；`LoadConfigForHero` 从内存英雄存储选择方案，不每次重载整份磁盘档案。
5. `FileTransaction` 对单文件临时写入、Flush、替换。INI 与 JSON 依次保存，不是跨文件事务；后一文件失败明确报告部分成功。

加载先区分英雄 sidecar 缺失与读取/解析失败。只有缺失且 INI 存在旧英雄节时才迁移；有效空 JSON 仍是该档案的权威数据，不借旧 INI 回填。损坏或读取受阻时保留当前内存与磁盘，记录失败路径；自动、退出和手动英雄保存均不能覆盖它，成功重载后解除保护。

`LoadConfig` 返回 `ConfigLoadResult`：`Failed` 未加载新档案；`Loaded` 正常完成；`MigrationSaveFailed` 已加载新档案但迁移写盘失败。UI 对前者恢复旧档案名，对后者保留新档案及待存状态并报告失败，避免把新参数写入旧档案。启动与 `--config-check` 对未完全成功返回非零。合法旧版本检查仍可能执行迁移，故 `--config-check` 仍只在隔离副本验证，不能称为只读命令。

诊断 effective 策略来自当前统一采集：目标 `acquisition_target_hz=300`、整数目标周期 `3333µs`；旧环境开关不能改变这些值。实际 Hz/age 仍来自计数器。`critical_batch_fusion_*` 和 `viewmatrix_poll_sleep_ms` 保留旧兼容含义，不用它们推导实际采集节拍。

## 构建与验证入口

在产品目录执行：

```powershell
# 正式程序：build/Release/UNDMA_V2.exe
./build-release.ps1

# 离线测试产物在 Scenario，不进入产品 build
./build-release.ps1 -Tests

# 成功构建后，使用 VS 附带或已入 PATH 的同版本 ctest
ctest --test-dir ../../04_SCENARIO/un-dma-v2/build/selftests -C Release --output-on-failure
```

脚本查找 Visual Studio Community 2026 的 CMake。正式 target 仍名 `Unleashed`，EXE 为 `UNDMA_V2.exe`。正常入口 `./start-v2.ps1`，显式 TestServer 默认 19560，V1 为 19550；两版不能并发占同一 DMA。

`UNLEASHED_OBSERVATION_ONLY` 是关闭物理输出/目标写入的编译选项；观察构建与证据放 Scenario，不覆盖正式程序。TestServer 未 arm 仅限制该 API 写入口，不表示普通程序业务输出全部关闭。屏幕流源身份固定：主机 UDP 9808，本地/副机 UDP 9809。

### 2026-11-03 起始只读审查状态（修复前）

| 项目 | 结果 | 边界 |
|---|---|---|
| 完整自测构建 | 4 目标失败：DiscoveryFairness、EntityRegistryRelocation、EntityRegistryProducer、HealthFailure | 对应 7 个用例未运行 |
| 其余 CTest | 43 项运行，42 通过，HeroSkillConfig 失败 | 不能写成全套通过 |
| HeroSkillConfig 定位 | 仅加失败行号的隔离副本报原测试 377 行 | 旧 E 键断言与已记录的回声右键默认值变更失配 |
| 隔离配置检查 | 损坏英雄 JSON 被覆盖，退出 0 | 确认加载失败处理缺陷，不是有效配置验收 |
| 正常 DMA/外设会话 | 本轮未运行 | 不新增实机、视觉、性能或长跑结论 |

详见 [只读审查报告](../../../04_SCENARIO/un-dma-v2/reports/readonly-audit-20261103/REVIEW.zh-CN.md)，含调用链、精简建议、反证及未查范围。此前新构建 21 分钟现场观察留在 [实施进度](V2_PROGRESS.zh-CN.md)，不能与本轮离线结果拼成超过 40 分钟或全部场景通过。

## 按修改风险选择验证

| 修改入口 | 要追踪的调用/失败路径 | 最贴近的已有验证 |
|---|---|---|
| 配置/Slot | UI、英雄切换、迁移、自动/退出保存、快照并发 | ConfigPersistence、ConfigConcurrency、AimPresetConfig；含损坏/受阻加载保护与失败迁移待存 |
| 实体/发现/缓存 | 完整/部分扫描、迟到结果、配对替换、搬家、消费权限 | EntityRepair、Lifecycle/Registry/Health；覆盖 fairness/relocation/late-result |
| 输出/协议 | 入队→发送→完成→取消→释放→换代 | OutputQueue、Lifecycle、RuntimeSwitch、InputOwnership、OutputProtocol；实机另验 |
| Behavior/运动 | 参数语义、激活/释放、循环刷新、目标/Slot 失效 | Tracking/Flick/FirePhase/Hybrid/Prediction/Cadence；手感和全英雄另验 |
| 会话/内存 | 进入门、等锁、排空、worker 异常/返回 | RuntimeControl、MemoryDeviceRecovery、V2Recovery；拔插/永久阻塞另记 |
| 显示/诊断 | 同帧消费、新鲜度、插值、指标来源 | PresentationTiming、Overlay、ModelShapes、TestServerLifecycle；含旧开关 0/1 与 CN/BZ effective 响应（两者当前为长链） |

生产与测试仍通过部分 `.inl` 共享实现。改动局部类型、容器所有权或失败状态时要检查外部 fixture，正式 EXE 可编译不代表测试可编译。每次记录源码/程序版本、运行模式、实际命令结果、证据位置和未覆盖范围。静态确认、离线执行、真实连接、物理动作、画面对齐、长期稳定分别记账。

有实际竞态窗口的重复检查应保留依据；无变化发布、死分支和失效包装可逐项小步精简。不要借整理改动 UN 的业务语义，也不先建设大框架。


## 2026-11-03 审查项修复后的验证

F1–F3、C1–C3 已按本日用户指示处理。全套自测构建成功，50/50 CTest 通过；配置持久化 79/79、配置并发 38/38。正式 Release 重建成功，新 EXE 的隔离合法配置检查退出 0，截断/数组根英雄 JSON 均退出 1且文件不变。正式配置 5 份文件保持原散列。

F2 fixture 已接回共享 record、紧凑读取和不可变发布帧。布局变化立即撤销旧身体并整组重建，普通运输失败仍使用 100ms 宽限，13s 席位与迟到结果权限检查保留。空扫描没有删除权限。不得为了旧断言回退这些现行规则。公平性测试的历史调度驱动只保证所覆盖的共享边界；现行统一发现入口另由 `EntityRepairSelfTest` 直接执行，不把 4096 候选测试当成统一调度器端到端性能验收。

详细改动、断言调整依据、程序散列、保全和未覆盖范围见 [修复记录](../../../04_SCENARIO/un-dma-v2/reports/audit-fixes-20261103/RESULT.zh-CN.md)。本轮未启动真实 DMA/外设会话，不更新物理效果、UI 手工操作或长跑结论。
