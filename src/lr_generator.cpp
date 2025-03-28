#include "seuyacc/lr_generator.h"
#include <iostream>
#include <sstream>

namespace seuyacc {

LRGenerator::LRGenerator(const YaccParser& p)
    : parser(p)
{
    // 初始化
}

void LRGenerator::generateTable()
{
    // 构建规范项集族
    buildCanonicalCollection();

    // 构建动作和转移表
    buildActionGotoTable();
}

std::unordered_set<Symbol, SymbolHasher> LRGenerator::computeFirst(const Symbol& symbol)
{
    // 检查缓存
    if (first_cache.find(symbol) != first_cache.end()) {
        // 已经计算过，直接返回缓存结果，不输出任何信息
        return first_cache[symbol];
    }

    // 防止左递归导致的栈溢出：先插入空集
    std::unordered_set<Symbol, SymbolHasher> result;
    first_cache[symbol] = result;

    // 终结符或字面量的FIRST集就是其自身
    if (symbol.type == ElementType::TOKEN || symbol.type == ElementType::LITERAL) {
        result.insert(symbol);
        first_cache[symbol] = result;
        return result;
    }

    // 对非终结符，查找所有以该非终结符为左部的产生式
    int matchCount = 0;
    bool changed = true;

    // 使用固定点算法：不断扩展结果，直到不再变化
    while (changed) {
        changed = false;
        // 对非终结符，查找所有以该非终结符为左部的产生式
        for (const Production& prod : parser.productions) {
            if (prod.left.name == symbol.name) {
                matchCount++;

                // 如果右部为空，添加空符号
                if (prod.right.empty()) {
                    Symbol epsilon = { "ε", ElementType::TOKEN };
                    if (result.insert(epsilon).second) {
                        changed = true;
                    }
                    continue;
                }

                // 处理右部第一个符号
                int i = 0;
                bool allCanDeriveEmpty = true;

                while (i < prod.right.size() && allCanDeriveEmpty) {
                    // 如果右部第i个符号就是正在计算的符号，则跳过
                    if (prod.right[i].name == symbol.name && prod.right[i].type == symbol.type) {
                        i++;
                        continue;
                    }

                    // 计算右部第i个符号的FIRST集
                    auto firstOfRight = computeFirst(prod.right[i]);

                    // 检查是否可以推导出空
                    Symbol epsilon = { "ε", ElementType::TOKEN };
                    allCanDeriveEmpty = (firstOfRight.find(epsilon) != firstOfRight.end());

                    // 将非空符号添加到结果
                    for (const auto& s : firstOfRight) {
                        if (s.name != "ε" && result.insert(s).second) {
                            changed = true;
                        }
                    }

                    if (!allCanDeriveEmpty) {
                        break;
                    }
                    i++;
                }

                // 如果右部所有符号都可以推导出空，则结果中添加空符号
                if (allCanDeriveEmpty) {
                    Symbol epsilon = { "ε", ElementType::TOKEN };
                    if (result.insert(epsilon).second) {
                        changed = true;
                    }
                }
            }
        }
    }

    if (matchCount == 0) {
        std::cout << "  警告: 没有找到非终结符 " << symbol.name << " 的产生式!" << std::endl;
    }

    // 更新缓存
    first_cache[symbol] = result;

    return result;
}

std::unordered_set<Symbol, SymbolHasher> LRGenerator::computeFirstOfSequence(const std::vector<Symbol>& sequence)
{
    std::unordered_set<Symbol, SymbolHasher> result;

    if (sequence.empty()) {
        // 空序列的FIRST集包含ε
        Symbol epsilon = { "ε", ElementType::TOKEN };
        result.insert(epsilon);
        return result;
    }

    // 计算序列第一个符号的FIRST集
    auto firstOfFirst = computeFirst(sequence[0]);
    result.insert(firstOfFirst.begin(), firstOfFirst.end());

    // 如果序列还有更多符号且当前符号可能推导出空，则继续计算
    int i = 1;
    Symbol epsilon = { "ε", ElementType::TOKEN };
    while (i < sequence.size() && firstOfFirst.find(epsilon) != firstOfFirst.end()) {
        // 移除epsilon，因为它不会出现在FIRST(αβ)中，除非所有符号都能推导出空
        result.erase(epsilon);

        // 计算下一个符号的FIRST集
        firstOfFirst = computeFirst(sequence[i]);
        result.insert(firstOfFirst.begin(), firstOfFirst.end());
        i++;
    }

    return result;
}

void LRGenerator::buildActionGotoTable()
{
    // 从项集规范族构建ACTION和GOTO表
    for (const ItemSet& state : canonical_collection) {
        int stateId = state.state_id;

        // 处理每个项
        for (const LRItem& item : state.items) {
            // 如果点号在最右边，这是一个规约项
            if (item.dot_position == item.prod.right.size()) {
                // 特殊处理增广产生式 S' -> S·
                if (item.prod.left.name == "S'" && item.lookahead.name == "$") {
                    // 接受动作
                    action_table[stateId][item.lookahead] = { ActionType::ACCEPT, 0 };
                } else {
                    // 规约动作，找出产生式在原文法中的索引
                    int prodIndex = 0;
                    for (size_t i = 0; i < parser.productions.size(); ++i) {
                        if (parser.productions[i].left.name == item.prod.left.name && parser.productions[i].right == item.prod.right) {
                            prodIndex = i;
                            break;
                        }
                    }

                    // 添加规约动作
                    action_table[stateId][item.lookahead] = { ActionType::REDUCE, prodIndex };
                }
            }
        }

        // 处理转移
        for (const StateTransition& transition : transitions) {
            if (transition.from_state == stateId) {
                if (transition.symbol.type == ElementType::TOKEN) {
                    // 移入动作
                    action_table[stateId][transition.symbol] = { ActionType::SHIFT, transition.to_state };
                } else {
                    // GOTO表项
                    goto_table[stateId][transition.symbol] = transition.to_state;
                }
            }
        }
    }
}

ItemSet LRGenerator::computeClosure(const ItemSet& itemSet)
{
    ItemSet result = itemSet;
    bool changed = true;
    int addedItems = 0;

    while (changed) {
        changed = false;
        std::vector<LRItem> items = result.items;

        for (const LRItem& item : items) {
            // 如果点号后面有非终结符 B
            if (item.dot_position < item.prod.right.size() && item.prod.right[item.dot_position].type == ElementType::NON_TERMINAL) {

                Symbol B = item.prod.right[item.dot_position];

                std::vector<Symbol> betaA;
                // 构造 β a
                for (int i = item.dot_position + 1; i < item.prod.right.size(); ++i) {
                    betaA.push_back(item.prod.right[i]);
                }
                betaA.push_back(item.lookahead);

                // 计算 FIRST(βa)
                std::unordered_set<Symbol, SymbolHasher> firstSet = computeFirstOfSequence(betaA);

                // 对于每个形如 B → γ 的产生式
                int matchedProductions = 0;
                for (const Production& p : parser.productions) {
                    if (p.left.name == B.name) {
                        matchedProductions++;

                        // 对每个 b ∈ FIRST(βa)
                        for (const Symbol& b : firstSet) {
                            LRItem newItem = { p, 0, b };

                            // 检查新项是否已存在
                            bool exists = false;
                            for (const auto& existingItem : result.items) {
                                if (existingItem == newItem) {
                                    exists = true;
                                    break;
                                }
                            }

                            if (!exists) {
                                result.items.push_back(newItem);
                                changed = true;
                                addedItems++;
                            }
                        }
                    }
                }
                if (matchedProductions == 0) {
                    std::cout << "    警告: 没有找到非终结符 " << B.name << " 的产生式!" << std::endl;
                }
            }
        }
    }
    return result;
}

ItemSet LRGenerator::computeGoto(const ItemSet& itemSet, const Symbol& symbol)
{
    ItemSet resultSet;

    for (const LRItem& item : itemSet.items) {
        // 如果点号后面是要查找的符号
        if (item.dot_position < item.prod.right.size() && item.prod.right[item.dot_position].name == symbol.name && item.prod.right[item.dot_position].type == symbol.type) {

            // 创建新项，将点号向右移动一位
            LRItem newItem = item;
            newItem.dot_position++;
            resultSet.items.push_back(newItem);
        }
    }

    // 如果resultSet不为空，计算其闭包
    if (!resultSet.items.empty()) {
        return computeClosure(resultSet);
    }

    return resultSet;
}

void LRGenerator::addAugmentedProduction()
{
    // 创建一个新的开始符号 S'
    Symbol newStart = { "S'", ElementType::NON_TERMINAL };

    // 创建增广产生式 S' -> S
    Production augmentedProd;
    augmentedProd.left = newStart;

    // 添加原文法的开始符号作为右部
    Symbol originalStart = { parser.start_symbol, ElementType::NON_TERMINAL };
    augmentedProd.right.push_back(originalStart);

    // 创建终止符号 $
    Symbol endSymbol = { "$", ElementType::TOKEN };

    // 将增广产生式添加到产生式列表的开头
    parser.productions.insert(parser.productions.begin(), augmentedProd);
}

void LRGenerator::buildCanonicalCollection()
{
    // 首先添加增广文法的起始项
    addAugmentedProduction();

    if (parser.productions.empty()) {
        std::cerr << "错误: 产生式列表为空!" << std::endl;
        return;
    }

    // 创建初始项集
    ItemSet initialItemSet;
    LRItem initialItem = {
        parser.productions[0], // 增广文法的第一条产生式
        0, // 点号位置在最左边
        { "$", ElementType::TOKEN } // 向前看符号为终止符
    };
    initialItemSet.items.push_back(initialItem);
    initialItemSet.state_id = 0;

    ItemSet closure0 = computeClosure(initialItemSet);

    canonical_collection.push_back(closure0);

    // 使用工作表算法构建项集规范族
    std::vector<ItemSet> worklist = { closure0 };

    while (!worklist.empty()) {
        ItemSet current = worklist.back();
        worklist.pop_back();

        // 收集当前项集中点号后的所有符号
        std::unordered_set<Symbol, SymbolHasher> symbols;
        for (const LRItem& item : current.items) {
            if (item.dot_position < item.prod.right.size()) {
                symbols.insert(item.prod.right[item.dot_position]);
            }
        }

        // 对每个符号计算GOTO
        for (const Symbol& X : symbols) {
            ItemSet gotoSet = computeGoto(current, X);

            if (gotoSet.items.empty()) {
                continue;
            }

            // 检查是否为新项集
            bool isNew = true;
            int existingIndex = -1;

            for (size_t i = 0; i < canonical_collection.size(); ++i) {
                if (canonical_collection[i] == gotoSet) {
                    isNew = false;
                    existingIndex = i;
                    break;
                }
            }

            if (isNew) {
                gotoSet.state_id = canonical_collection.size();
                canonical_collection.push_back(gotoSet);
                worklist.push_back(gotoSet);

                // 添加转移
                transitions.push_back({ current.state_id, gotoSet.state_id, X });
            } else {
                // 添加到已有项集的转移
                transitions.push_back({ current.state_id, existingIndex, X });
            }
        }
    }

    std::cout << "规范项集族构建完成, 共 " << canonical_collection.size() << " 个状态, "
              << transitions.size() << " 个转移" << std::endl;
}

std::string LRGenerator::toPlantUML() const
{
    std::stringstream ss;
    ss << "@startuml\n";
    ss << "[*] --> State0\n";

    // 添加所有状态及其项集内容
    for (const ItemSet& itemSet : canonical_collection) {
        ss << "State" << itemSet.state_id << " : ";

        // 使用一个映射来分组相同产生式但不同lookahead的项
        std::map<std::string, std::vector<Symbol>> groupedItems;

        // 分组项
        for (const LRItem& item : itemSet.items) {
            // 构建产生式和点号位置的唯一表示（作为键）
            std::stringstream itemKey;
            itemKey << item.prod.left.name << " -> ";

            // 右侧包含点号位置
            for (size_t i = 0; i < item.prod.right.size(); ++i) {
                if (i == item.dot_position) {
                    itemKey << "• ";
                }
                itemKey << item.prod.right[i].name << " ";
            }

            // 如果点号在最右边
            if (item.dot_position == item.prod.right.size()) {
                itemKey << "• ";
            }

            // 将lookahead添加到对应键的集合中
            groupedItems[itemKey.str()].push_back(item.lookahead);
        }

        // 现在输出分组后的项
        for (const auto& group : groupedItems) {
            ss << group.first << ", ";

            // 输出所有lookahead，用"/"分隔
            for (size_t i = 0; i < group.second.size(); ++i) {
                ss << group.second[i].name;
                if (i < group.second.size() - 1) {
                    ss << "/";
                }
            }
            ss << "\\n";
        }
        ss << "\n";
    }

    // 添加所有转移
    for (const StateTransition& transition : transitions) {
        ss << "State" << transition.from_state << " --> ";
        ss << "State" << transition.to_state << " : ";
        ss << transition.symbol.name << "\n";
    }

    ss << "@enduml\n";
    return ss.str();
}
}