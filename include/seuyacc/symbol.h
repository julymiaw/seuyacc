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

// 枚举表示运算符的结合性
enum class Associativity {
    NONE,
    LEFT,
    RIGHT,
    NONASSOC
};

// 语法符号 - 可以是终结符、非终结符、操作符等
struct Symbol {
    int id = -1; // 唯一id，默认-1
    std::string name;
    ElementType type;
    std::string value_type; // 符号值的类型（来自%type或%token<type>）
    int precedence; // 优先级，数字越大优先级越高
    Associativity assoc; // 结合性

    // 构造函数，设置默认值
    Symbol()
        : id(-1)
        , type(ElementType::TOKEN)
        , precedence(0)
        , assoc(Associativity::NONE)
    {
    }

    // 兼容旧代码的构造函数
    Symbol(const std::string& n, ElementType t)
        : id(-1)
        , name(n)
        , type(t)
        , precedence(0)
        , assoc(Associativity::NONE)
    {
    }

    // 添加比较运算符

    bool operator==(const Symbol& other) const
    {
        if (id != -1 && other.id != -1)
            return id == other.id;
        return name == other.name && type == other.type;
    }

    // 为map排序添加小于比较
    bool operator<(const Symbol& other) const
    {
        if (id != -1 && other.id != -1)
            return id < other.id;
        if (name != other.name)
            return name < other.name;
        return static_cast<int>(type) < static_cast<int>(other.type);
    }
};

// 为Symbol添加哈希函数
struct SymbolHasher {
    size_t operator()(const Symbol& symbol) const
    {
        if (symbol.id != -1)
            return std::hash<int> {}(symbol.id);
        return std::hash<std::string> {}(symbol.name) ^ (std::hash<int> {}(static_cast<int>(symbol.type)) << 1);
    }
};

} // namespace seuyacc

#endif // SEUYACC_SYMBOL_H