#include "seuyacc/lr_item.h"

namespace seuyacc {

bool LRItem::operator==(const LRItem& other) const
{
    // 比较产生式左部
    if (!(prod.left == other.prod.left))
        return false;

    // 比较产生式右部
    if (prod.right.size() != other.prod.right.size())
        return false;

    for (size_t i = 0; i < prod.right.size(); ++i) {
        if (!(prod.right[i] == other.prod.right[i]))
            return false;
    }

    // 比较点号位置和向前看符号
    return dot_position == other.dot_position && (lookahead == other.lookahead);
}

size_t LRItem::hash() const
{
    size_t h1 = std::hash<std::string> {}(prod.left.name);
    size_t h2 = std::hash<int> {}(dot_position);
    size_t h3 = std::hash<std::string> {}(lookahead.name);
    size_t h4 = std::hash<int> {}(static_cast<int>(lookahead.type));

    // 组合哈希值
    size_t result = h1;
    result = (result << 1) ^ h2;
    result = (result << 1) ^ h3;
    result = (result << 1) ^ h4;

    // 添加产生式右部的哈希
    for (const auto& symbol : prod.right) {
        size_t symbolHash = std::hash<std::string> {}(symbol.name) ^ (std::hash<int> {}(static_cast<int>(symbol.type)) << 1);
        result = (result << 1) ^ symbolHash;
    }

    return result;
}

bool ItemSet::operator==(const ItemSet& other) const
{
    if (items.size() != other.items.size())
        return false;

    // 检查每个项是否都存在于other中
    for (const auto& item : items) {
        bool found = false;
        for (const auto& other_item : other.items) {
            if (item == other_item) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }

    return true;
}

} // namespace seuyacc