#ifndef SEUYACC_PRODUCTION_H
#define SEUYACC_PRODUCTION_H

#include "symbol.h"
#include <vector>

namespace seuyacc {

// 表示语法规则的产生式
struct Production {
    Symbol left; // 产生式左部
    std::vector<Symbol> right; // 产生式右部符号列表
    std::string semantic_action; // 语义动作代码
    int precedence; // 产生式的优先级，用于解决冲突

    // 构造函数，设置默认值
    Production()
        : precedence(0)
    {
    }
};

} // namespace seuyacc

#endif // SEUYACC_PRODUCTION_H