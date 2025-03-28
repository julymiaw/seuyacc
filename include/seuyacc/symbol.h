#ifndef SEUYACC_SYMBOL_H
#define SEUYACC_SYMBOL_H

#include <functional>
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

    // 添加比较运算符
    bool operator==(const Symbol& other) const
    {
        return name == other.name && type == other.type;
    }

    // 为map排序添加小于比较
    bool operator<(const Symbol& other) const
    {
        if (name != other.name)
            return name < other.name;
        return static_cast<int>(type) < static_cast<int>(other.type);
    }
};

// 为Symbol添加哈希函数
struct SymbolHasher {
    size_t operator()(const Symbol& symbol) const
    {
        return std::hash<std::string> {}(symbol.name) ^ (std::hash<int> {}(static_cast<int>(symbol.type)) << 1);
    }
};

} // namespace seuyacc

#endif // SEUYACC_SYMBOL_H