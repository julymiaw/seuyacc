#include "seuyacc/lr_generator.h"
#include <algorithm>
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
    int shift_reduce_conflicts = 0;
    int resolved_sr_conflicts = 0;
    int reduce_reduce_conflicts = 0;
    int resolved_rr_conflicts = 0;

    // 从项集规范族构建ACTION和GOTO表
    for (const ItemSet& state : canonical_collection) {
        int stateId = state.state_id;

        // 先处理所有规约项
        for (const LRItem& item : state.items) {
            // 如果点号在最右边，这是一个规约项
            if (item.dot_position == item.prod.right.size()) {
                // 特殊处理增广产生式 S' -> S·
                if (item.prod.left.name == "S'" && item.lookahead.name == "$") {
                    // 接受动作 - 优先级最高，不会有冲突
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

                    // 检查是否已经有针对该符号的动作
                    if (action_table[stateId].find(item.lookahead) != action_table[stateId].end()) {
                        ActionEntry existing = action_table[stateId][item.lookahead];

                        if (existing.type == ActionType::REDUCE) {
                            // 规约/规约冲突
                            reduce_reduce_conflicts++;

                            // 获取两个产生式
                            const Production& currentProd = parser.productions[prodIndex];
                            const Production& existingProd = parser.productions[existing.value];

                            // 检查是否可以用优先级解决
                            bool resolved = false;

                            // 如果两个产生式都有优先级，使用优先级高的
                            if (currentProd.precedence > 0 && existingProd.precedence > 0) {
                                if (currentProd.precedence > existingProd.precedence) {
                                    action_table[stateId][item.lookahead] = { ActionType::REDUCE, prodIndex };
                                    resolved = true;
                                } else if (currentProd.precedence < existingProd.precedence) {
                                    // 保留现有的动作
                                    resolved = true;
                                }
                                // 优先级相同的情况下，保留在文法中先出现的产生式
                            }

                            if (resolved) {
                                resolved_rr_conflicts++;
                                std::cout << "规约/规约冲突已解决(优先级): 状态 " << stateId
                                          << ", 符号 " << item.lookahead.name << std::endl;
                            } else {
                                std::cout << "规约/规约冲突: 状态 " << stateId
                                          << ", 符号 " << item.lookahead.name
                                          << ", 产生式 " << prodIndex << " 和产生式 " << existing.value << std::endl;

                                // 默认策略：选择在文法中较早出现的产生式
                                if (prodIndex < existing.value) {
                                    action_table[stateId][item.lookahead] = { ActionType::REDUCE, prodIndex };
                                }
                            }
                        }
                        // 这里不处理移入/规约冲突，因为处理规约项时不可能已经有移入项
                        // 移入/规约冲突在处理移入项时检测
                    } else {
                        // 没有冲突，添加规约动作
                        action_table[stateId][item.lookahead] = { ActionType::REDUCE, prodIndex };
                    }
                }
            }
        }

        // 然后处理所有移入项（即从该状态出发的转移）
        for (const StateTransition& transition : transitions) {
            if (transition.from_state == stateId) {
                if (transition.symbol.type == ElementType::TOKEN) {
                    // 终结符转移对应移入动作

                    // 检查是否已经有针对该符号的规约动作
                    if (action_table[stateId].find(transition.symbol) != action_table[stateId].end()) {
                        ActionEntry existing = action_table[stateId][transition.symbol];

                        if (existing.type == ActionType::REDUCE) {
                            // 移入/规约冲突
                            shift_reduce_conflicts++;

                            // 获取产生式和向前看符号的信息
                            const Production& reduceProd = parser.productions[existing.value];
                            const Symbol& lookAhead = transition.symbol;

                            // 检查是否可以用优先级和结合性解决
                            bool resolved = false;

                            // 如果产生式有优先级并且冲突符号也有优先级
                            if (reduceProd.precedence > 0 && parser.symbol_table.find(lookAhead.name) != parser.symbol_table.end() && parser.symbol_table.at(lookAhead.name).precedence > 0) {

                                const Symbol& conflictSymbol = parser.symbol_table.at(lookAhead.name);

                                // 比较优先级
                                if (reduceProd.precedence > conflictSymbol.precedence) {
                                    // 规约的优先级更高，选择规约（保留规约动作）
                                    resolved = true;
                                } else if (reduceProd.precedence < conflictSymbol.precedence) {
                                    // 移入的优先级更高，选择移入
                                    action_table[stateId][transition.symbol] = { ActionType::SHIFT, transition.to_state };
                                    resolved = true;
                                } else {
                                    // 优先级相同，使用结合性决定
                                    switch (conflictSymbol.assoc) {
                                    case Associativity::LEFT:
                                        // 左结合，选择规约（保留规约动作）
                                        resolved = true;
                                        break;
                                    case Associativity::RIGHT:
                                        // 右结合，选择移入
                                        action_table[stateId][transition.symbol] = { ActionType::SHIFT, transition.to_state };
                                        resolved = true;
                                        break;
                                    case Associativity::NONASSOC:
                                        // 无结合性，报错
                                        action_table[stateId][transition.symbol] = { ActionType::ERROR, 0 };
                                        resolved = true;
                                        std::cout << "无结合性操作符 (报错): 状态 " << stateId
                                                  << ", 符号 " << lookAhead.name << std::endl;
                                        break;
                                    default:
                                        // 无结合性信息
                                        break;
                                    }
                                }
                            }

                            if (resolved) {
                                resolved_sr_conflicts++;
                                // std::cout << "移入/规约冲突已解决(优先级/结合性): 状态 " << stateId
                                //           << ", 符号 " << lookAhead.name << std::endl;
                            } else {
                                std::cout << "移入/规约冲突: 状态 " << stateId
                                          << ", 符号 " << transition.symbol.name
                                          << ", 移入到状态 " << transition.to_state
                                          << " 或规约产生式 " << existing.value << std::endl;

                                // 默认策略：选择移入
                                action_table[stateId][transition.symbol] = { ActionType::SHIFT, transition.to_state };
                            }
                        }
                        // 不应该已经有SHIFT或ACCEPT
                    } else {
                        // 没有冲突，添加移入动作
                        action_table[stateId][transition.symbol] = { ActionType::SHIFT, transition.to_state };
                    }
                } else {
                    // 非终结符转移对应GOTO表项
                    goto_table[stateId][transition.symbol] = transition.to_state;
                }
            }
        }
    }

    // 报告冲突总数
    std::cout << "\n==== 冲突统计 ====" << std::endl;
    std::cout << "移入/规约冲突: " << shift_reduce_conflicts << " 个，已解决: " << resolved_sr_conflicts << " 个" << std::endl;
    std::cout << "规约/规约冲突: " << reduce_reduce_conflicts << " 个，已解决: " << resolved_rr_conflicts << " 个" << std::endl;
    std::cout << "==================" << std::endl;

    if (shift_reduce_conflicts == resolved_sr_conflicts && reduce_reduce_conflicts == resolved_rr_conflicts) {
        if (shift_reduce_conflicts > 0 || reduce_reduce_conflicts > 0) {
            std::cout << "所有冲突已通过优先级和结合性规则解决！" << std::endl;
        } else {
            std::cout << "文法没有冲突，是LALR(1)文法！" << std::endl;
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

// 获取所有终结符（按名称排序）
std::vector<Symbol> LRGenerator::getSortedTerminals() const
{
    std::vector<Symbol> terminals;

    // 收集所有终结符，包括EOF符号$
    Symbol endSymbol = { "$", ElementType::TOKEN };
    terminals.push_back(endSymbol);

    for (const auto& [name, symbol] : parser.symbol_table) {
        if (symbol.type == ElementType::TOKEN && name != "$" && name != "ε") {
            terminals.push_back(symbol);
        }
    }

    // 按名称排序
    std::sort(terminals.begin(), terminals.end(), [](const Symbol& a, const Symbol& b) {
        return a.name < b.name;
    });

    return terminals;
}

// 获取所有非终结符（按名称排序）
std::vector<Symbol> LRGenerator::getSortedNonTerminals() const
{
    std::vector<Symbol> nonTerminals;

    for (const auto& [name, symbol] : parser.symbol_table) {
        if (symbol.type == ElementType::NON_TERMINAL && name != "S'") {
            nonTerminals.push_back(symbol);
        }
    }

    // 按名称排序
    std::sort(nonTerminals.begin(), nonTerminals.end(), [](const Symbol& a, const Symbol& b) {
        return a.name < b.name;
    });

    return nonTerminals;
}

// 将ActionEntry转换为可读字符串
std::string LRGenerator::actionEntryToString(const ActionEntry& entry) const
{
    switch (entry.type) {
    case ActionType::SHIFT:
        return "s" + std::to_string(entry.value);
    case ActionType::REDUCE:
        return "r" + std::to_string(entry.value);
    case ActionType::ACCEPT:
        return "acc";
    case ActionType::ERROR:
        return "err";
    default:
        return "";
    }
}

// 找到产生式的字符串表示
std::string LRGenerator::getProductionString(int index) const
{
    if (index >= 0 && index < parser.productions.size()) {
        const Production& prod = parser.productions[index];
        std::stringstream ss;

        ss << prod.left.name << " -> ";

        if (prod.right.empty()) {
            ss << "ε";
        } else {
            for (const auto& symbol : prod.right) {
                ss << symbol.name << " ";
            }
        }

        return ss.str();
    }

    return "未知产生式";
}

// 从Union代码中提取YYSTYPE定义
std::string LRGenerator::extractYYSTYPE() const
{
    std::string unionCode = parser.union_code;
    if (unionCode.empty()) {
        // 没有定义union，返回默认的YYSTYPE
        return "typedef int YYSTYPE;";
    }

    // 提取union代码块内容
    size_t start = unionCode.find('{');
    size_t end = unionCode.rfind('}');

    if (start != std::string::npos && end != std::string::npos && start < end) {
        // 返回整个union定义
        return "typedef union " + unionCode.substr(start, end - start + 1) + " YYSTYPE;";
    }

    // 如果解析失败，返回默认定义
    return "typedef int YYSTYPE;";
}

/// 将ACTION和GOTO表导出为Markdown格式
std::string LRGenerator::toMarkdownTable() const
{
    std::stringstream ss;

    // 生成标题和基本信息
    ss << "# LR(1) 分析表\n\n";
    ss << "## 基本信息\n\n";
    ss << "- 状态数量: " << canonical_collection.size() << "\n";
    ss << "- 终结符数量: " << getSortedTerminals().size() - 1 << " (不含 $)\n";
    ss << "- 非终结符数量: " << getSortedNonTerminals().size() << "\n";
    ss << "- 产生式数量: " << parser.productions.size() << "\n\n";

    // 列出所有产生式
    ss << "## 产生式列表\n\n";
    for (size_t i = 0; i < parser.productions.size(); ++i) {
        const Production& prod = parser.productions[i];
        ss << "- (" << i << ") " << prod.left.name << " -> ";

        if (prod.right.empty()) {
            ss << "ε";
        } else {
            for (size_t j = 0; j < prod.right.size(); ++j) {
                ss << prod.right[j].name;
                // 只在非最后一个符号后添加空格
                if (j < prod.right.size() - 1) {
                    ss << " ";
                }
            }
        }

        // 添加优先级和结合性信息（如果有）
        if (prod.precedence > 0) {
            ss << " [优先级: " << prod.precedence << "]";
        }

        ss << "\n";
    }
    ss << "\n";

    // 收集终结符和非终结符
    std::vector<Symbol> terminals = getSortedTerminals();
    std::vector<Symbol> nonTerminals = getSortedNonTerminals();

    // 生成ACTION表
    ss << "## ACTION表\n\n";
    ss << "| 状态 |";

    // ACTION表头：终结符
    for (const auto& terminal : terminals) {
        ss << " " << terminal.name << " |";
    }
    ss << "\n| --- |";

    // 表头分隔线
    for (size_t i = 0; i < terminals.size(); ++i) {
        ss << " --- |";
    }
    ss << "\n";

    // ACTION表内容：每个状态对每个终结符的动作
    for (int state = 0; state < canonical_collection.size(); ++state) {
        ss << "| " << state << " |";

        for (const auto& terminal : terminals) {
            if (action_table.find(state) != action_table.end() && action_table.at(state).find(terminal) != action_table.at(state).end()) {
                ss << " " << actionEntryToString(action_table.at(state).at(terminal)) << " |";
            } else {
                ss << " |";
            }
        }

        ss << "\n";
    }
    ss << "\n";

    // 生成GOTO表
    ss << "## GOTO表\n\n";
    ss << "| 状态 |";

    // GOTO表头：非终结符
    for (const auto& nonTerminal : nonTerminals) {
        ss << " " << nonTerminal.name << " |";
    }
    ss << "\n| --- |";

    // 表头分隔线
    for (size_t i = 0; i < nonTerminals.size(); ++i) {
        ss << " --- |";
    }
    ss << "\n";

    // GOTO表内容：每个状态对每个非终结符的转移
    for (int state = 0; state < canonical_collection.size(); ++state) {
        ss << "| " << state << " |";

        for (const auto& nonTerminal : nonTerminals) {
            if (goto_table.find(state) != goto_table.end() && goto_table.at(state).find(nonTerminal) != goto_table.at(state).end()) {
                ss << " " << goto_table.at(state).at(nonTerminal) << " |";
            } else {
                ss << " |";
            }
        }

        ss << "\n";
    }
    ss << "\n";

    // 添加规约产生式的详细信息
    ss << "## 规约说明\n\n";
    ss << "| 规约动作 | 产生式 | 说明 |\n";
    ss << "| --- | --- | --- |\n";
    for (size_t i = 0; i < parser.productions.size(); ++i) {
        const Production& prod = parser.productions[i];
        ss << "| r" << i << " | " << prod.left.name << " -> ";

        if (prod.right.empty()) {
            ss << "ε";
        } else {
            for (size_t j = 0; j < prod.right.size(); ++j) {
                ss << prod.right[j].name;
                // 只在非最后一个符号后添加空格
                if (j < prod.right.size() - 1) {
                    ss << " ";
                }
            }
        }

        ss << " | 规约为 " << prod.left.name;

        // 添加语义动作提示（如果有）
        if (!prod.semantic_action.empty()) {
            ss << "，执行语义动作";
        }

        ss << " |\n";
    }

    // 去除最后一个多余的换行符
    std::string result = ss.str();

    return result;
}

// 导出C语言头文件
std::string LRGenerator::generateHeaderFile(const std::string& filename) const
{
    std::stringstream ss;

    std::string headerGuard = filename;
    std::transform(headerGuard.begin(), headerGuard.end(), headerGuard.begin(),
        [](unsigned char c) { return std::toupper(c); });
    std::replace(headerGuard.begin(), headerGuard.end(), '.', '_');

    // 添加简洁的头文件注释
    ss << "/* 由 SeuYacc 生成的 LR(1) 解析器头文件 */\n\n";

    // 添加头文件保护宏
    ss << "#ifndef " << headerGuard << "_INCLUDED\n";
    ss << "# define " << headerGuard << "_INCLUDED\n";

    // 添加调试跟踪的宏定义
    ss << "/* 调试跟踪设置 */\n";
    ss << "#ifndef YYDEBUG\n";
    ss << "# define YYDEBUG 0\n";
    ss << "#endif\n";
    ss << "#if YYDEBUG\n";
    ss << "extern int yydebug;\n";
    ss << "#endif\n\n";

    // 添加令牌类型定义
    ss << "/* 令牌类型定义 */\n";
    ss << "#ifndef YYTOKENTYPE\n";
    ss << "# define YYTOKENTYPE\n";
    ss << "  enum yytokentype\n";
    ss << "  {\n";
    ss << "    YYEMPTY = -2,\n";
    ss << "    YYEOF = 0,                     /* \"文件结束\" */\n";
    ss << "    YYerror = 256,                 /* 错误 */\n";
    ss << "    YYUNDEF = 257,                 /* \"无效令牌\" */\n";

    // 获取所有终结符并按名称排序
    std::vector<Symbol> terminals = getSortedTerminals();
    int tokenValue = 258; // 从258开始，前面已经使用了一些特殊值

    // 添加终结符令牌定义（不包括$和ε）
    for (const auto& terminal : terminals) {
        if (terminal.name != "$" && terminal.name != "ε") {
            std::string tokenName = terminal.name;
            // 如果是字面量，转换为有效的C标识符
            if (terminal.type == ElementType::LITERAL) {
                tokenName = "LITERAL_" + std::to_string(tokenValue);
            }
            ss << "    " << tokenName << " = " << tokenValue << ",";
            // 添加可选注释
            if (terminal.type == ElementType::LITERAL) {
                ss << "                 /* " << terminal.name << " */";
            }
            ss << "\n";
            tokenValue++;
        }
    }

    ss << "  };\n";
    ss << "  typedef enum yytokentype yytoken_kind_t;\n";
    ss << "#endif\n\n";

    // 添加令牌定义宏
    ss << "/* 令牌定义宏 */\n";
    ss << "#define YYEMPTY -2\n";
    ss << "#define YYEOF 0\n";
    ss << "#define YYerror 256\n";
    ss << "#define YYUNDEF 257\n";

    tokenValue = 258;
    for (const auto& terminal : terminals) {
        if (terminal.name != "$" && terminal.name != "ε") {
            std::string tokenName = terminal.name;
            if (terminal.type == ElementType::LITERAL) {
                tokenName = "LITERAL_" + std::to_string(tokenValue);
            }
            ss << "#define " << tokenName << " " << tokenValue << "\n";
            tokenValue++;
        }
    }
    ss << "\n";

    // 添加值类型定义
    ss << "/* 值类型定义 */\n";
    ss << "#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED\n";

    // 从文法中提取union定义
    if (!parser.union_code.empty()) {
        size_t lineNumber = 1;
        std::string unionContent;
        std::istringstream unionStream(parser.union_code);
        std::string line;

        // 跳过开头的 %{ 或 %}
        while (std::getline(unionStream, line)) {
            if (line.find("{") != std::string::npos) {
                unionContent += "union YYSTYPE\n{\n";
                break;
            }
            lineNumber++;
        }

        // 处理union内容
        while (std::getline(unionStream, line)) {
            if (line.find("}") != std::string::npos) {
                unionContent += "};\n";
                break;
            }
            unionContent += line + "\n";
            lineNumber++;
        }

        ss << unionContent << "\n";
        ss << "typedef union YYSTYPE YYSTYPE;\n";
        ss << "# define YYSTYPE_IS_TRIVIAL 1\n";
        ss << "# define YYSTYPE_IS_DECLARED 1\n";
    } else {
        // 如果没有定义union，提供默认定义
        ss << "union YYSTYPE\n";
        ss << "{\n";
        ss << "  int ival;\n";
        ss << "  char* sval;\n";
        ss << "};\n";
        ss << "typedef union YYSTYPE YYSTYPE;\n";
        ss << "# define YYSTYPE_IS_TRIVIAL 1\n";
        ss << "# define YYSTYPE_IS_DECLARED 1\n";
    }
    ss << "#endif\n\n";

    // 添加外部变量声明
    ss << "\n/* 外部变量声明 */\n";
    ss << "extern YYSTYPE yylval;\n\n";

    // 添加解析函数声明
    ss << "\n/* 解析函数声明 */\n";
    ss << "int yyparse(void);\n\n";

    // 添加头文件结尾保护宏
    ss << "\n#endif /* !" << headerGuard << "_INCLUDED */\n";

    return ss.str();
}
}