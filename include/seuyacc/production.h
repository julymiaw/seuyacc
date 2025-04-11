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
};

} // namespace seuyacc

#endif // SEUYACC_PRODUCTION_H