#ifndef SEUYACC_LR_ITEM_H
#define SEUYACC_LR_ITEM_H

#include "production.h"
#include "symbol.h"
#include <vector>

namespace seuyacc {

// LR(1)项，表示 A → α·β, a 的结构
struct LRItem {
    Production prod; // 相关产生式
    int dot_position; // 点号位置
    Symbol lookahead; // 向前看符号

    // 用于项集比较和存储在集合中
    bool operator==(const LRItem& other) const;
    size_t hash() const;
};

// 项集，包含多个LR(1)项
struct ItemSet {
    std::vector<LRItem> items;
    int state_id; // 状态ID，用于生成分析表

    bool operator==(const ItemSet& other) const;
};

// 用于在无序集合中作为键
struct LRItemHasher {
    size_t operator()(const LRItem& item) const
    {
        return item.hash();
    }
};

// 状态转移
struct StateTransition {
    int from_state; // 源状态ID
    int to_state; // 目标状态ID
    Symbol symbol; // 转移符号
};

} // namespace seuyacc

#endif // SEUYACC_LR_ITEM_H