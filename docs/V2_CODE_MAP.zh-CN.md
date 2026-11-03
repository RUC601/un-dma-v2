# V2 代码阅读地图与保留契约

2026-11-03：完整业务流程、当前目录责任和验证入口见 [V2 开发手册](V2_DEVELOPMENT_GUIDE.zh-CN.md)；本轮 [只读审查](../../../04_SCENARIO/un-dma-v2/reports/readonly-audit-20261103/REVIEW.zh-CN.md) 区分已检查/未检查范围及未证实项。以下是模块导航，不代表全部路径已验收。

更新：2026-10-29。本文描述当前实现；验证状态见 `V2_PROGRESS.zh-CN.md` 和 `V2_DELIVERY.zh-CN.md`。文件位置按本仓库相对路径表示。Windows 的大小写兼容不意味着已完成 Linux 移植。

## 从启动到显示和输出

`src/main.cpp` 加载本地配置、解析运行参数、建立窗口和装配会话。`runtime/ProcessSession` 管理 DMA 附加、布局路由、线程启动、重连和排空。CN 通过映像及布局核验后，启动统一采集、UN Behavior、英雄配置和技能工作线程；显示插值按配置独立启动。2026-10-30 接入 C 后，采集线程按固定 300Hz 高精度周期推进相机、实体热读取和分步发现，相机散读句柄复用。

统一采集线程发布带身份和时间的共享不可变消费帧。`SceneRenderer` 和 `BehaviorController` 持有消费帧，`EntityVisuals` 绘制；行为层调用保留的 UN 目标选择和运动算法，产生动作。`output/Actions` 先检查会话、设备代次、菜单和业务授权，再进入设备调度。文件拆分没有把这些步骤变成新进程或 DLL。

| 阅读入口 | 输入 → 产出 | 状态所有者 | 失败路径 |
|---|---|---|---|
| `src/runtime/ProcessSession.cpp`、`WorkerGroup.cpp` | 配置、目标身份 → 已验证会话/工作线程 | 连接线程、WorkerGroup；生命周期门与最终停止共用 | 关输出、取消队列、join 工作线程，随后解绑/关闭 DMA；线程异常触发重连 |
| `src/Game/profiles/LayoutProbe.hpp`、`ProfileState.hpp` | 精确读取、当前会话断言 → 兼容性证据/路由 | 连接层发布状态，UI 读取副本 | 短读、歧义、会话变化拒绝；手选不能授权未匹配布局 |
| `src/Game/profiles/CandidateSession.cpp` | KR/BZ 候选 Profile、读取接口 → 候选 parent 统计 | 单个 CandidateWorker 持有 cursor/keys/header | 每步复核，结束复核 keys/header；失败清结果；不启动 UN/输出 |
| `src/Game/entities/RegistryResolver.hpp` | PE 节、已知位置、读取结果 → 经语义核验的根位置 | CN reader lane 的 resolver；候选绑定会话 | 有限步进、每步语义预算、稀疏尾部、多候选、迁表/过期拒绝或重试 |
| `src/Game/entities/EntityDiscovery.cpp`、`EntityReader.cpp` | 表/拓扑 → 已发现对象和当前可用身体 | 统一采集 worker 持有发现 cursor、拓扑和字段状态；旧维护状态仍由 `Decrypt.hpp`、`Overwatch.hpp` 提供 | partial 不获得 complete 的删除权限；短读/身份变化不发布旧身体 |
| `src/Game/camera/CameraWorker.cpp` | 相机链 → 矩阵及发布时间 | 统一采集 worker 调用采样，复用散读句柄；发布锁保护样本 | 无效/过期不供消费者使用 |
| `src/Game/snapshots/SnapshotAccess.hpp`、`PresentationWorker.cpp` | 共享不可变消费帧 → 同帧消费/可选插值 | 采集端发布；消费者持有 const 帧及其生命周期 | connection/scene/身体新鲜度检查；未提供有效值则返回空 |
| `src/Features/aim/BehaviorController.cpp`、`SessionState.cpp` | 有效实体、配置快照、激活输入 → 目标与动作 | 单个 Behavior worker 的 session 状态；稳定 Slot ID | 断连、换场景/Slot、输入释放使锁存失效；Flick 武器冷却保留 |
| `src/Features/aim/Smoothing.cpp`、`include/Game/Target.hpp` | UN 目标/方法参数 → 角度和运动结果 | 运动状态按线程隔离；小数学保留 inline | 无效目标/配置门控返回；不改算法含义来适配 Pure |
| `src/output/Actions.cpp`、`PhysicalMask.cpp`、`DeviceConnection.cpp` | 业务动作 → 有代次的设备命令/释放 | 输出调度、物理屏蔽 lease 协调器、设备运行层各持有自身状态 | 不支持明确拒绝；旧代次不能进入新后端；切换先停发和释放 |
| `src/Features/visuals/SceneRenderer.cpp`、`EntityVisuals.cpp` | 消费快照 → DX11/ImGui 绘制 | UI/渲染线程持有绘制缓存 | 无有效身体/矩阵不作为正常对象绘制；目标端画面对齐仍须现场验 |
| `src/ui/WorkspaceShell.cpp`、`pages/*`、`SlotSelector.cpp` | 编辑操作 → 配置命令 | UI 编辑上下文；业务线程不调用 ImGui | 按上下文显示停用、保存失败及连接状态；设备能力不足不冒充可用 |
| `src/config/HeroSlotStore.cpp`、`FileTransaction.cpp`、`HeroProfileWorker.cpp` | Slot/英雄编辑 → 规范化配置/磁盘事务/运行快照 | Config mutex/ScopedConfigAuthority；运行时取值 | 拒绝删空、修复旧 ID、保存错误返回；英雄切换保存失败延后 |

仍保留的集中实现：`include/Game/Overwatch.hpp` 的实体维护与发布状态、`Decrypt.hpp` 的 CN 解码/扫描协议、`Target.hpp` 的目标选择、`include/Utils/Config.hpp` 与 `src/Utils/Config.cpp` 的 schema/迁移。它们是明确的后续维护入口，不能把本轮描述成“所有全局状态已消除”。新模块是正式编译单元，不是只移动 `.inl`。

## 一次设置怎样生效

1. 左侧“瞄准”选英雄和方案；`AimPage.cpp` 取配置值，按 Behavior 显示适用参数。
2. `SlotSelector` 只返回选择/新增/删除命令，`UI.cpp` 通过 `HeroSlotStore` 操作。新增复制参数、清两类绑定、默认停用，分配新的非零 `uint64_t stableId`。显示序号可以变化，ID 不随重排变化。
3. `ScopedConfigEdit`/配置锁提交编辑；运行层的 `View_*`/英雄 override 从配置快照取值，不把 UI 容器引用带入工作线程。绑定且启用的方案按原 UN 规则选择，编辑选中不等于功能激活。
4. “保存全部”进入 `SaveAllConfig` 与 `FileTransaction`；JSON/INI 用字符串保存 ID，旧文件加载时补齐/修复。错误在 UI 保留。重新加载恢复参数、停用状态、绑定与 ID。
5. 删除或替换已引用的 Slot 时，Tracking/Trigger/Flick 检查稳定 ID，清目标连续性/运动与激活锁存；同一武器的射击冷却不能借换 Slot 清零。

## UN Behavior 与参数关系

Behavior 决定动作语义；Method 决定如何运动。两者不合并，也不换成 Pure 的分类。

| Behavior | 专属/关联规则 | 共享配置入口 |
|---|---|---|
| Tracking | 连续跟随、目标连续性、deadzone/damping | 方法、速度倍率、启动限制、FOV/目标、预测 |
| Flick | 激活周期、到位/射击与武器冷却、shot clamp/post-fire | 方法与预设、速度倍率、目标/预测、分段移动 |
| Flick2nd | 原二阶段/扳机门槛、内圈范围/方法/速度 | Flick 通用参数和 Method；不改成另一种武器行为 |
| Reacquire | 保留原 UN 的重新捕获路径与选择策略 | Behavior 预设/Method；不另造 Pure 等价算法 |
| Magnetic Trigger | deadzone、磁吸时序/轴倍率、射击控制 | Method、目标/FOV、预测与射击门槛 |

代码契约在 `Config.hpp` 的 `Is*Behavior`、`UsesTrackingDeadzone`、`UsesFlickFireControls` 和 `AimBehavior*` 系列；页内条件直接采用同一组规则。具名 Behavior preset 匹配对应 Behavior 后提供方法/速度/启动限制/分段参数；Method preset 管方法参数；Slot 保存自身绑定、启用、FOV/目标与倍率等。旧配置的规范化和默认回退保留，未改变参数单位。

保留 PID、Bezier、分段、恒角速度、Hybrid、overshoot、预测、二阶段与英雄技能/序列；`SmoothingInternal.hpp` 仅为已有状态检查保留入口。不能以“已链接”证明所有英雄的现场手感等价。

## 生命周期保留清单

- V1 的资源和配置作为独立初始副本继承，后续 V2 写入只在自己的运行目录。
- entity identity、connection epoch、scene generation、100 ms 身体和 13 s 席位规则保持；名单存在不等于身体当前可用。
- 菜单编辑与画布显示语义保留；菜单态不获得正常动作授权。
- 停止先关新动作授权并通知生产者，再取消/释放、join；DMA 与输出资源只在其消费者退出后关闭。SDK 无法中断的单次调用仍须等待返回，不以原子标志宣称可取消永久阻塞。
- worker 入口返回、join 完成、资源关闭是不同事实。日志和 `RuntimeControlSelfTest` 支持有限生命周期检查，不构成长时间/所有硬件异常验收。
- 运行时统计与目标端可见效果分别验收；只读 API 未 arm 只限制该 API 写入口，不等于正式程序所有功能都处于 dry-run。


## 输出设备代码入口（2026-11-02）

公共接口在 `include/output`，实现在 `src/output`：`OutputRuntime` 管代次、切换与分发；`OutputTypes` 管通用命令和完成状态；`SerialTransport` 管唯一串口读入口、队列和生命周期。`KmBoxNetProtocol` 只定义 KMBox 网络报文；`KmBoxSerialProtocol`、`MakcuProtocol`、`FerrumProtocol` 区分串口协议。`CatBoxProtocol` 管加密和报文、`CatBoxBackend` 管网络工作线程。`MockBackend` 和 `TimerResolution` 使用中性名称。

旧 `src/Kmbox` 与 `include/Kmbox` 已删除，不保留转发头文件。旧配置字段仅在配置兼容边界保留名称；KMBox 品牌仅用于其专属实现和协议。
