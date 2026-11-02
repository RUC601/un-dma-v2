#pragma once
// 自己拥有去抖状态；连接代次改变时丢弃候选。保存失败则延后切换。
void configsavenloadthread();
