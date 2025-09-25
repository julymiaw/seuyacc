#include "seuyacc/lr_item.h"

namespace seuyacc {

bool LRItem::operator==(const LRItem& other) const
{
    // 只需比较产生式id、点号位置和lookahead
    return prod.id == other.prod.id && dot_position == other.dot_position && lookahead == other.lookahead;
}

size_t LRItem::hash() const
{
    // 只用产生式id、点号位置和lookahead做hash
    size_t h1 = std::hash<int> {}(prod.id);
    size_t h2 = std::hash<int> {}(dot_position);
    size_t h3 = SymbolHasher {}(lookahead);
    return ((h1 << 1) ^ h2) ^ (h3 << 2);
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