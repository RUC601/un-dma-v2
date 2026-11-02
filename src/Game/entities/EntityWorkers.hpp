#pragma once
// 会话只创建一条采集线程，按热帧、发现小步、到期慢维护顺序推进。
// 行为与渲染持有只读消费帧，资源在采集线程返回前释放。
void StepAcquisitionDiscovery();
void entity_thread();
