# V2 当前交付与验收清单

## 2026-10-30 当前交付

正式 V2 已接入 C 的统一采集、紧凑热读取、分步发现、骨骼缓存和共享消费帧，采集固定 300Hz，无频率选择参数。正式入口 start-v2.ps1，程序 build/Release/UNDMA_V2.exe，TestServer 默认 19560，沿用本工作区配置及资源。

本次 Release 构建通过；45 秒 CN 短跑启动被自动审批以 blocked by policy 拒绝，程序未启动，本次没有新的现场验收。C 历史 300Hz 运行有实际采样波动，不能写作每实体稳定达到 300Hz。

B/C 已整目录归档至 ../../06_ARCHIVE/un-v2-experiments-20261030/，含 Git、C 未提交内容、配置、日志及运行产物。B 未合入主线，C 的实验入口及历史 240Hz 材料留在归档。以下为此前交付记录，旧验收不外推到本次构建。

2026-10-29 用户反馈更新：用户确认上一轮列出的两项现场检查已人工做过，反馈“挺好”。因此 CN 目标选择/动作及目标端显示对齐、输出/释放，记录为**用户人工检查通过**。这补齐了此前等待的两项使用检查；不是新增自动遥测或录像证据，不外推为全部英雄、长跑、同场景 V1/V2 定量对照通过。

正式程序可启动，CN 采集、矩阵/实体发布、重连和正常退出已有机器记录。下文的“待用户现场检查”和“尚未验证”保留为上一交付时点记录，其两项状态以本次用户反馈为准。仍未完成的根恢复现场、新设备、候选地区完整业务、全界面检查及进一步模块整理见能力表/阅读地图；不宣称总纲所有扩展项均完成。

## 入口

- 桌面 `D:/Desktop/UN V2.lnk`，目标 `build/Release/UNDMA_V2.exe`，工作目录为本仓库 `build/Release`，已读取快捷方式复核。
- `start-v2.ps1`；需要只读遥测时加 `-TestServer`，端口19560。
- V1 独立入口 `../un-dma-v1/start-v1.ps1`。先正常退出一版再启动另一版，不并发占用DMA。
- 配置在各自 EXE 旁的 `config`；本轮不替换V1配置。

## 逐项验收

| 总纲要求 | 当前证据 | 结论 |
|---|---|---|
| 保留V1、独立源码/配置/运行产物 | `v1-preservation-current.json`：HEAD、干净状态、EXE和5份配置hash一致；快捷方式独立 | 已核对 |
| 模块化、所有权、阅读地图 | `V2_CODE_MAP.zh-CN.md`，CMake正式编译单元，会话/工作线程/输出/快照/页面/存储边界 | 已实施；保留的旧集中点明确列出 |
| UN算法/Behavior/Slot语义 | 代码地图保留契约、稳定ID、47项回归、实际UI事件/磁盘重载 | 离线通过；所有英雄现场手感未验证 |
| CN完整运行 | `live-final-route` 70秒、136样本、epoch1→2、退出0；矩阵/实体有效 | 采集/重连通过，目标与输出效果待验 |
| 根恢复和多Profile | V2RecoverySelfTest、RegistryResolver、CandidateWorker | 异常边界离线通过；根搜索后采用的现场闭环未触发 |
| KR/BZ候选实现 | LayoutProbe、CandidateAdapter/Session、显式CLI/UI路由 | 已接入候选读取/解码；无UN字段全链，不启用输出；按约定不做实服 |
| 统一输出与多设备 | Actions/DeviceConnection/PhysicalMask、能力表、队列/切换检查、真实Net初始化/释放 | 代码接入；目标端效果及新增实物设备待验，缺协议设备明示不可用 |
| 新UI与配置 | `V2_UI_REVIEW.zh-CN.md`；双语700×680/200%、320px Aim、键盘和保存重载 | 限定流程通过；未宣称全域无障碍 |
| 配置检查 | `config-gate-confirmed.json`：最新正式EXE退出0且配置hash不变 | 最新构建已验证 |
| 最新源回归 | `build-gate-tests.log` 构建成功；`ctest-gate-final.log` 47/47，20.47秒 | 离线回归通过 |
| 性能/画面对照 | 各连接固定12秒后的数据年龄，原始样本保留 | 有有限运行统计；没有同场景V1/V2手感、画面对齐与长跑结论 |

证据根：`../../00_META/v2-implementation-20261029/`。`live-final-route` EXE SHA256 `372132485A1F8E66DD44FAD18F76CB2141710EFBA2A26B9FC69542330B954836`，此后还有UI长标签和低频门控诊断改动。最新程序以随后运行目录的 `run.json` 为准，不能拿旧hash说明新字节。

## 恢复与保全

2026-10-29 用户要求先提交、剩余之后再做。本次本地提交保存当前改造和人工验收记录，不推送，没有切换或reset V1。此前保全时尚未提交，`before` 保存V2原HEAD、原dirty patch、原AGENTS/计划/研究/V2说明和V1基准。`source-inventory-current.json` 包含保全时tracked与非忽略untracked的内容hash，生成时点见文件；提交后的文档更新以Git为准。

恢复日常使用只需正常退出V2，启动独立V1，无需回退代码。若以后要撤销V2改造，应先把当前源码、未提交文件、配置和本轮证据另外保全，再按 `before/v2-head.txt` 和 `before/v2.patch` 重建到**新目录**核对；不得直接对现有dirty工作区执行hard reset、clean或覆盖。原用户的计划/AGENTS修改也在保留范围。

## 待用户现场检查

低频门控诊断、最新正式实连、配置核查、最终回归和交付保全均已完成。剩余 CN 效果验证需要用户真实操作，不能用模拟输入、放宽门槛或重复无输入采样代替：

1. 正常退出 V1 后，从桌面“UN V2”进入合适 CN 场景，实际按下当前英雄/Slot 绑定，检查目标选择与动作；菜单编辑选中不等于启用或激活。
2. 恢复主机 9808 画面或由用户直接观察目标端，检查显示对齐、输出及释放；此前两次没有完整帧，9809 不能代替。
3. 同场景串行比较 V1/V2 操作体验；根据实际反馈继续定位。当前没有证明所有英雄手感、长跑或新增设备效果。

KR/BZ 实服不在本轮待办。根恢复现场未触发、新设备缺实物、Controller/Medius 缺可信协议继续作为局部未解决项保留，见能力表。按用户原指令暂停等待检查，总目标未完成。

## 可恢复交付副本

`handoff-final` 为人工验收反馈之前的不可变交付快照；本次仅更新文档记录，不覆盖包和摘要。人工反馈不绑定新的程序hash，也不改变此前程序身份记录。

证据根 `handoff-final/` 保存独立的 `source.zip`、`runtime.zip`、`source-manifest.json`、`runtime-manifest.json`、当前未提交 patch/status 和 `verification.json`。源码包包含 Git tracked 与非忽略 untracked 文件；运行包包含正式 EXE/DLL、info.db、assets/data/config，不包含日志、崩溃转储和 PDB。包内文件逐项 SHA256 回读核对，包本身摘要及 V1 复核结果见 verification.json。配置只在本机保全，未上传。

这两个包为当前工作快照，源码包不含 Git 历史；可在新目录恢复核对，不覆盖 V1 或当前工作区。需要调试符号时原 build/Release/UNDMA_V2.pdb 仍保留。正式入口继续使用原 V2 工作目录。

## 最新程序身份补充

17:13正式程序SHA256：`DA3E172607DAF5F2EC818F1EA047571D5BCA13F5FAF6CC452755078569509F29`；`live-gate-confirmed/run.json`记录35秒、68样本、正常退出0。包含最新UI长标签和低频运行门控诊断。稳定时点tracking=1、localBody/matrix/entities=1、input=0；目标端激活后的选择/输出仍未验证。此次未做重连，重连证据来自此前`live-final-route`，不混为同一范围。
