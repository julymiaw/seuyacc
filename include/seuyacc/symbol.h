#ifndef SEUYACC_SYMBOL_H
#define SEUYACC_SYMBOL_H

#include <string>

namespace seuyacc {

// 枚举表示不同的语法元素类型
enum class ElementType {
    TOKEN,
    NON_TERMINAL,
    LITERAL
};

// 语法符号 - 可以是终结符、非终结符、操作符等
struct Symbol {
    std::string name;
    ElementType type;
};

} // namespace seuyacc

#endif // SEUYACC_SYMBOL_H