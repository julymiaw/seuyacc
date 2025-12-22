#include "seuyacc/lr_generator.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace seuyacc {

LRGenerator::LRGenerator(const YaccParser& p)
    : parser(p)
{
    // 初始化
}

void LRGenerator::generateTable()
{
    first_cache.clear();
    canonical_collection.clear();
    transitions.clear();
    action_table.clear();
    goto_table.clear();

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

    const Symbol& epsilon_symbol = parser.getSymbol("ε");

    // 终结符或字面量的FIRST集就是其自身
    if (symbol.type == ElementType::TOKEN || symbol.type == ElementType::LITERAL) {
        result.insert(symbol);
        first_cache[symbol] = result;
        return result;
    }

    Symbol::ensureIdAssigned(symbol, "LRGenerator::computeFirst");

    std::vector<const Production*> candidates;
    candidates.reserve(8);
    for (const auto& prod : parser.productions) {
        if (prod.left.id == symbol.id) {
            candidates.push_back(&prod);
        }
    }

    if (candidates.empty()) {
        std::cout << "  警告: 没有找到非终结符 " << symbol.name << " 的产生式!" << std::endl;
        first_cache[symbol] = result;
        return result;
    }
    bool changed = true;

    // 使用固定点算法：不断扩展结果，直到不再变化
    while (changed) {
        changed = false;
        for (const auto* prodPtr : candidates) {
            const Production& prod = *prodPtr;

            // 如果右部为空，添加空符号
            if (prod.right.empty()) {
                if (result.insert(epsilon_symbol).second) {
                    changed = true;
                }
                continue;
            }

            int i = 0;
            bool allCanDeriveEmpty = true;

            while (i < static_cast<int>(prod.right.size()) && allCanDeriveEmpty) {
                const Symbol& current = prod.right[i];

                // 如果右部第i个符号就是正在计算的符号，则跳过
                if (current.name == symbol.name && current.type == symbol.type) {
                    i++;
                    continue;
                }

                auto firstOfRight = computeFirst(current);
                allCanDeriveEmpty = (firstOfRight.find(epsilon_symbol) != firstOfRight.end());

                for (const auto& s : firstOfRight) {
                    if (s.name != "ε" && result.insert(s).second) {
                        changed = true;
                    }
                }

                i++;
            }

            if (!allCanDeriveEmpty) {
                continue;
            }

            if (result.insert(epsilon_symbol).second) {
                changed = true;
            }
        }
    }

    // 更新缓存
    first_cache[symbol] = result;

    return result;
}

std::unordered_set<Symbol, SymbolHasher> LRGenerator::computeFirstOfSequence(const std::vector<Symbol>& sequence)
{
    std::unordered_set<Symbol, SymbolHasher> result;
    const Symbol& epsilon_symbol = parser.getSymbol("ε");

    if (sequence.empty()) {
        // 空序列的FIRST集包含ε
        result.insert(epsilon_symbol);
        return result;
    }

    // 计算序列第一个符号的FIRST集
    auto firstOfFirst = computeFirst(sequence[0]);
    result.insert(firstOfFirst.begin(), firstOfFirst.end());

    // 如果序列还有更多符号且当前符号可能推导出空，则继续计算
    int i = 1;
    while (i < sequence.size() && firstOfFirst.find(epsilon_symbol) != firstOfFirst.end()) {
        // 移除epsilon，因为它不会出现在FIRST(αβ)中，除非所有符号都能推导出空
        result.erase(epsilon_symbol);

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

    for (const ItemSet& state : canonical_collection) {
        const int stateId = state.state_id;

        for (const LRItem& item : state.items) {
            if (isReduceItem(item)) {
                applyReduceAction(stateId, item, reduce_reduce_conflicts, resolved_rr_conflicts);
            }
        }

        for (const StateTransition& transition : transitions) {
            if (transition.from_state != stateId) {
                continue;
            }

            if (transition.symbol.type == ElementType::NON_TERMINAL) {
                goto_table[stateId][transition.symbol] = transition.to_state;
            } else {
                applyShiftAction(stateId, transition, shift_reduce_conflicts, resolved_sr_conflicts);
            }
        }
    }

    reportConflictStats(shift_reduce_conflicts, resolved_sr_conflicts, reduce_reduce_conflicts, resolved_rr_conflicts);
}

bool LRGenerator::isReduceItem(const LRItem& item) const
{
    return item.dot_position >= static_cast<int>(item.prod.right.size());
}

int LRGenerator::productionIndexOf(const LRItem& item) const
{
    const int candidateId = item.prod.id;
    if (candidateId >= 0 && candidateId < static_cast<int>(parser.productions.size())) {
        const Production& candidate = parser.productions[candidateId];
        if (candidate.left.name == item.prod.left.name && candidate.right == item.prod.right) {
            return candidateId;
        }
    }

    for (size_t i = 0; i < parser.productions.size(); ++i) {
        const Production& prod = parser.productions[i];
        if (prod.left.name == item.prod.left.name && prod.right == item.prod.right) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

bool LRGenerator::resolveReduceReduceConflict(int newProdIndex, ActionEntry& existingEntry, int& resolvedCount) const
{
    if (existingEntry.value < 0 || existingEntry.value >= static_cast<int>(parser.productions.size())) {
        return false;
    }

    const Production& currentProd = parser.productions[newProdIndex];
    const Production& existingProd = parser.productions[existingEntry.value];

    if (currentProd.precedence > 0 && existingProd.precedence > 0) {
        if (currentProd.precedence > existingProd.precedence) {
            existingEntry = { ActionType::REDUCE, newProdIndex };
            resolvedCount++;
            return true;
        }
        if (currentProd.precedence < existingProd.precedence) {
            resolvedCount++;
            return true;
        }
    }

    return false;
}

bool LRGenerator::resolveShiftReduceConflict(int stateId, const StateTransition& transition, int reduceIndex, ActionEntry& existingEntry, int& resolvedCount) const
{
    if (reduceIndex < 0 || reduceIndex >= static_cast<int>(parser.productions.size())) {
        return false;
    }

    const Production& reduceProd = parser.productions[reduceIndex];
    const Symbol& lookAhead = transition.symbol;

    auto symbolIt = parser.symbol_table.find(lookAhead.name);
    if (reduceProd.precedence > 0 && symbolIt != parser.symbol_table.end() && symbolIt->second.precedence > 0) {

        const Symbol& conflictSymbol = symbolIt->second;

        if (reduceProd.precedence > conflictSymbol.precedence) {
            resolvedCount++;
            return true; // 保留规约
        }

        if (reduceProd.precedence < conflictSymbol.precedence) {
            existingEntry = { ActionType::SHIFT, transition.to_state };
            resolvedCount++;
            return true;
        }

        switch (conflictSymbol.assoc) {
        case Associativity::LEFT:
            resolvedCount++;
            return true; // 左结合，选择规约
        case Associativity::RIGHT:
            existingEntry = { ActionType::SHIFT, transition.to_state };
            resolvedCount++;
            return true;
        case Associativity::NONASSOC:
            existingEntry = { ActionType::ERROR, 0 };
            resolvedCount++;
            std::cout << "无结合性操作符 (报错): 状态 " << stateId
                      << ", 符号 " << lookAhead.name << std::endl;
            return true;
        default:
            break;
        }
    }

    return false;
}

void LRGenerator::applyReduceAction(int stateId, const LRItem& item, int& conflictCount, int& resolvedCount)
{
    if (item.prod.left.name == "S'" && item.lookahead.name == "$") {
        action_table[stateId][item.lookahead] = { ActionType::ACCEPT, 0 };
        return;
    }

    const int prodIndex = productionIndexOf(item);
    if (prodIndex < 0) {
        std::cout << "  警告: 无法确定产生式索引: " << item.prod.left.name << " -> ";
        if (item.prod.right.empty()) {
            std::cout << "ε";
        } else {
            for (const Symbol& sym : item.prod.right) {
                std::cout << sym.name << ' ';
            }
        }
        std::cout << std::endl;
        return;
    }

    auto& actions = action_table[stateId];
    auto it = actions.find(item.lookahead);
    if (it == actions.end()) {
        actions[item.lookahead] = { ActionType::REDUCE, prodIndex };
        return;
    }

    ActionEntry& existingEntry = it->second;
    if (existingEntry.type != ActionType::REDUCE) {
        return;
    }

    conflictCount++;

    if (resolveReduceReduceConflict(prodIndex, existingEntry, resolvedCount)) {
        return;
    }

    std::cout << "规约/规约冲突: 状态 " << stateId
              << ", 符号 " << item.lookahead.name
              << ", 产生式 " << prodIndex << " 和产生式 " << existingEntry.value << std::endl;

    if (prodIndex < existingEntry.value) {
        existingEntry = { ActionType::REDUCE, prodIndex };
    }
}

void LRGenerator::applyShiftAction(int stateId, const StateTransition& transition, int& conflictCount, int& resolvedCount)
{
    auto& actions = action_table[stateId];
    auto it = actions.find(transition.symbol);
    if (it == actions.end()) {
        actions[transition.symbol] = { ActionType::SHIFT, transition.to_state };
        return;
    }

    ActionEntry& existingEntry = it->second;
    if (existingEntry.type != ActionType::REDUCE) {
        return;
    }

    conflictCount++;

    if (resolveShiftReduceConflict(stateId, transition, existingEntry.value, existingEntry, resolvedCount)) {
        return;
    }

    std::cout << "移入/规约冲突: 状态 " << stateId
              << ", 符号 " << transition.symbol.name
              << ", 移入到状态 " << transition.to_state
              << " 或规约产生式 " << existingEntry.value << std::endl;

    actions[transition.symbol] = { ActionType::SHIFT, transition.to_state };
}

void LRGenerator::reportConflictStats(int shiftReduceConflicts, int resolvedSR, int reduceReduceConflicts, int resolvedRR) const
{
    std::cout << "\n==== 冲突统计 ====" << std::endl;
    std::cout << "移入/规约冲突: " << shiftReduceConflicts << " 个，已解决: " << resolvedSR << " 个" << std::endl;
    std::cout << "规约/规约冲突: " << reduceReduceConflicts << " 个，已解决: " << resolvedRR << " 个" << std::endl;
    std::cout << "==================" << std::endl;

    if (shiftReduceConflicts == resolvedSR && reduceReduceConflicts == resolvedRR) {
        if (shiftReduceConflicts > 0 || reduceReduceConflicts > 0) {
            std::cout << "所有冲突已通过优先级和结合性规则解决！" << std::endl;
        }
    }
}

ItemSet LRGenerator::computeClosure(const ItemSet& itemSet)
{
    ItemSet result = itemSet;
    size_t index = 0;

    while (index < result.items.size()) {
        const LRItem& item = result.items[index++];

        if (item.dot_position >= item.prod.right.size()) {
            continue;
        }

        const Symbol& nextSymbol = item.prod.right[item.dot_position];
        if (nextSymbol.type != ElementType::NON_TERMINAL) {
            continue;
        }

        std::vector<Symbol> betaA;
        betaA.reserve(item.prod.right.size() - item.dot_position);
        betaA.insert(betaA.end(), item.prod.right.begin() + item.dot_position + 1, item.prod.right.end());
        betaA.push_back(item.lookahead);

        std::unordered_set<Symbol, SymbolHasher> firstSet = computeFirstOfSequence(betaA);

        bool matchedProduction = false;
        for (const Production& p : parser.productions) {
            if (p.left.name != nextSymbol.name) {
                continue;
            }

            matchedProduction = true;

            for (const Symbol& lookahead : firstSet) {
                LRItem newItem { p, 0, lookahead };

                if (std::find(result.items.begin(), result.items.end(), newItem) == result.items.end()) {
                    result.items.push_back(newItem);
                }
            }
        }

        if (!matchedProduction) {
            std::cout << "    警告: 没有找到非终结符 " << nextSymbol.name << " 的产生式!" << std::endl;
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
    Symbol& newStart = parser.ensureSymbol("S'", ElementType::NON_TERMINAL);

    Production augmentedProd;
    augmentedProd.left = newStart;

    Symbol& originalStart = parser.ensureSymbol(parser.start_symbol, ElementType::NON_TERMINAL);
    augmentedProd.right.push_back(originalStart);

    parser.productions.insert(parser.productions.begin(), augmentedProd);

    for (size_t i = 0; i < parser.productions.size(); ++i) {
        parser.productions[i].id = static_cast<int>(i);
    }
}

// 处理语义动作中的 $$ 和 $N 替换
std::string LRGenerator::processSemanticAction(const std::string& action, const Production& prod) const
{
    if (action.empty()) {
        return "/* 无语义动作 */";
    }

    std::string processed = action;
    std::string result;

    // 处理 $$ (左值赋值)
    size_t pos = 0;
    while ((pos = processed.find("$$", pos)) != std::string::npos) {
        std::string replacement = "yyval";

        // 如果非终结符有类型信息，添加适当的成员访问
        if (!prod.left.value_type.empty()) {
            replacement += "." + prod.left.value_type;
        }

        processed.replace(pos, 2, replacement);
        pos += replacement.length();
    }

    // 处理 $N (右值引用)
    pos = 0;
    while (pos < processed.length()) {
        if (processed[pos] == '$' && pos + 1 < processed.length() && std::isdigit(processed[pos + 1])) {

            size_t start = pos;
            pos++; // 跳过 $

            // 读取数字
            std::string num;
            while (pos < processed.length() && std::isdigit(processed[pos])) {
                num += processed[pos];
                pos++;
            }

            int index = std::stoi(num);

            // 确保索引有效
            if (index > 0 && index <= prod.right.size()) {
                // 关键修改: 使用正确的索引对应关系
                std::string replacement = "yyvsp[" + num + "]";

                // 如果对应的符号有类型信息，添加适当的成员访问
                const Symbol& sym = prod.right[index - 1];
                if (!sym.value_type.empty()) {
                    replacement += "." + sym.value_type;
                }

                processed.replace(start, num.length() + 1, replacement);
                pos = start + replacement.length();
            }
        } else {
            pos++;
        }
    }

    return processed;
}

// 生成语法分析器代码
std::string LRGenerator::generateParserCode(const std::string& filename) const
{
    std::stringstream ss;

    // 添加头部注释和包含文件
    ss << "/* 由 SeuYacc 生成的 LR(1) 解析器 */\n\n";

    // 包含生成的头文件
    std::string headerName = filename;
    size_t dot_pos = headerName.find_last_of('.');
    if (dot_pos != std::string::npos) {
        headerName = headerName.substr(0, dot_pos) + ".h";
    } else {
        headerName += ".h";
    }

    ss << "#include \"" << headerName << "\"\n";
    ss << "#include <stdio.h> /* 包含标准输入输出库，因为使用了printf */\n";
    ss << "#include <stdlib.h>\n";
    ss << "#include <string.h>\n\n";

    // 添加错误收集结构
    ss << "/* 错误收集功能 */\n";
    ss << "typedef struct ErrorInfo {\n";
    ss << "    int line;\n";
    ss << "    char* message;\n";
    ss << "    char* actual_token;\n";
    ss << "    char** expected_tokens;\n";
    ss << "    int expected_count;\n";
    ss << "} ErrorInfo;\n\n";
    
    ss << "static ErrorInfo* errors = NULL;\n";
    ss << "static int error_count = 0;\n";
    ss << "static int error_capacity = 0;\n\n";
    
    ss << "static void add_error(int line, const char* msg, const char* actual, \n";
    ss << "                     const char** expected, int exp_count) {\n";
    ss << "    if (error_count >= error_capacity) {\n";
    ss << "        error_capacity = error_capacity == 0 ? 10 : error_capacity * 2;\n";
    ss << "        errors = (ErrorInfo*)realloc(errors, error_capacity * sizeof(ErrorInfo));\n";
    ss << "    }\n";
    ss << "    ErrorInfo* err = &errors[error_count];\n";
    ss << "    err->line = line;\n";
    ss << "    err->message = strdup(msg);\n";
    ss << "    err->actual_token = actual ? strdup(actual) : NULL;\n";
    ss << "    err->expected_count = exp_count;\n";
    ss << "    if (exp_count > 0 && expected) {\n";
    ss << "        err->expected_tokens = (char**)malloc(exp_count * sizeof(char*));\n";
    ss << "        for (int i = 0; i < exp_count; i++) {\n";
    ss << "            err->expected_tokens[i] = strdup(expected[i]);\n";
    ss << "        }\n";
    ss << "    } else {\n";
    ss << "        err->expected_tokens = NULL;\n";
    ss << "    }\n";
    ss << "    error_count++;\n";
    ss << "}\n\n";
    
    ss << "static void print_errors_json(void) {\n";
    ss << "    printf(\"{\\n\");\n";
    ss << "    printf(\"  \\\"errors\\\": [\\n\");\n";
    ss << "    for (int i = 0; i < error_count; i++) {\n";
    ss << "        ErrorInfo* err = &errors[i];\n";
    ss << "        printf(\"    {\\n\");\n";
    ss << "        printf(\"      \\\"line\\\": %d,\\n\", err->line);\n";
    ss << "        printf(\"      \\\"message\\\": \\\"%s\\\",\\n\", err->message);\n";
    ss << "        if (err->actual_token) {\n";
    ss << "            printf(\"      \\\"actual\\\": \\\"%s\\\",\\n\", err->actual_token);\n";
    ss << "        }\n";
    ss << "        printf(\"      \\\"expected\\\": [\");\n";
    ss << "        for (int j = 0; j < err->expected_count; j++) {\n";
    ss << "            printf(\"\\\"%s\\\"\", err->expected_tokens[j]);\n";
    ss << "            if (j < err->expected_count - 1) printf(\", \");\n";
    ss << "        }\n";
    ss << "        printf(\"]\\n\");\n";
    ss << "        printf(\"    }%s\\n\", i < error_count - 1 ? \",\" : \"\");\n";
    ss << "    }\n";
    ss << "    printf(\"  ],\\n\");\n";
    ss << "    printf(\"  \\\"errorCount\\\": %d\\n\", error_count);\n";
    ss << "    printf(\"}\\n\");\n";
    ss << "}\n\n";

    // 添加用户声明代码块
    if (!parser.declaration_code.empty()) {
        ss << "/* 用户声明代码 */\n";
        ss << parser.declaration_code << "\n\n";
    }

    std::vector<Symbol> terminals = getSortedTerminals();
    std::vector<Symbol> nonTerminals = getSortedNonTerminals();
    std::vector<int> rawTokenValues = computeRawTokenValues(terminals);

    int yymaxutok = 0;
    for (int value : rawTokenValues) {
        if (value > yymaxutok) {
            yymaxutok = value;
        }
    }

    std::vector<int> translateTable(yymaxutok + 1, -1);
    for (size_t i = 0; i < rawTokenValues.size(); ++i) {
        translateTable[rawTokenValues[i]] = static_cast<int>(i);
    }

    // 添加全局变量定义
    ss << "/* 全局变量定义 */\n";
    ss << "YYSTYPE yylval;\n\n";

    // 添加解析器内部定义和宏
    ss << "/* 解析器内部定义 */\n";
    ss << "#ifndef YYMAXDEPTH\n";
    ss << "# define YYMAXDEPTH 10000\n"; // 添加YYMAXDEPTH定义
    ss << "#endif\n\n";
    ss << "#define YYFINAL " << (canonical_collection.size() - 1) << "\n";
    ss << "#define YYLAST " << (canonical_collection.size() * terminals.size()) << "\n\n";

    ss << "#define YYNTOKENS " << terminals.size() << "\n";
    ss << "#define YYNNTS " << nonTerminals.size() << "\n";
    ss << "#define YYNRULES " << parser.productions.size() << "\n";
    ss << "#define YYNSTATES " << canonical_collection.size() << "\n";
    ss << "#define YYMAXUTOK " << yymaxutok << "\n";
    ss << "#define YYUNDEF -1\n\n";

    ss << "static const short yytranslate_table[" << (yymaxutok + 1) << "] = {\n  ";
    for (int i = 0; i <= yymaxutok; ++i) {
        ss << translateTable[i];
        if (i != yymaxutok) {
            ss << ", ";
        }
        if ((i + 1) % 16 == 0 && i != yymaxutok) {
            ss << "\n  ";
        }
    }
    ss << "\n};\n\n";

    ss << "static inline int yytranslate_token(int token) {\n";
    ss << "  if (token < 0 || token > YYMAXUTOK) {\n";
    ss << "    return YYUNDEF;\n";
    ss << "  }\n";
    ss << "  return yytranslate_table[token];\n";
    ss << "}\n\n";

    // 添加动作和状态转移表
    ss << "/* 解析表 */\n";

    // 生成动作表
    ss << "static const short yytable[] = {\n";

    for (int state = 0; state < canonical_collection.size(); ++state) {
        ss << "  /* 状态 " << state << " */\n  ";
        for (const auto& terminal : terminals) {
            if (action_table.find(state) != action_table.end() && action_table.at(state).find(terminal) != action_table.at(state).end()) {

                const ActionEntry& entry = action_table.at(state).at(terminal);

                // 编码动作:
                // 正数 = 移入并转到该状态
                // 负数 = 按照产生式规约 (-规则号-1)
                // 0 = 接受
                int code;

                switch (entry.type) {
                case ActionType::SHIFT:
                    code = entry.value;
                    break;
                case ActionType::REDUCE:
                    code = -entry.value - 1;
                    break;
                case ActionType::ACCEPT:
                    code = 0;
                    break;
                default: // ERROR
                    code = -32767; // 表示错误
                }

                ss << code << ", ";
            } else {
                // 错误
                ss << "-32767, ";
            }
        }
        ss << "\n";
    }
    ss << "};\n\n";

    // 添加 token 名称表
    ss << "/* Token 名称表 */\n";
    ss << "static const char* yytname[] = {\n";
    ss << "  \"$end\"";
    for (const auto& terminal : terminals) {
        ss << ",\n  \"" << terminal.name << "\"";
    }
    ss << "\n};\n\n";

    // 生成GOTO表
    ss << "static const short yygoto[] = {\n";

    for (int state = 0; state < canonical_collection.size(); ++state) {
        ss << "  /* 状态 " << state << " */\n  ";
        for (const auto& nonTerminal : nonTerminals) {
            if (goto_table.find(state) != goto_table.end() && goto_table.at(state).find(nonTerminal) != goto_table.at(state).end()) {

                int nextState = goto_table.at(state).at(nonTerminal);
                ss << nextState << ", ";
            } else {
                // 无效状态
                ss << "-1, ";
            }
        }
        ss << "\n";
    }
    ss << "};\n\n";

    // 产生式左部的非终结符索引表
    ss << "/* 每条产生式左部的非终结符索引 */\n";
    ss << "static const short yyr1[] = {\n  ";

    // 计算非终结符的索引（从YYNTOKENS开始）
    std::unordered_map<std::string, int> nonterm_index;
    int idx = terminals.size(); // 非终结符索引从终结符数量开始
    for (const auto& nt : nonTerminals) {
        nonterm_index[nt.name] = idx++;
    }

    // 生成yyr1数组
    for (const auto& prod : parser.productions) {
        if (nonterm_index.find(prod.left.name) != nonterm_index.end()) {
            ss << nonterm_index[prod.left.name] << ", ";
        } else {
            ss << "0, "; // 默认值，理论上不会出现
        }
    }

    ss << "\n};\n\n";

    // 产生式长度表
    ss << "/* 每条产生式右部的符号数量 */\n";
    ss << "static const short yyr2[] = {\n  ";

    for (const auto& prod : parser.productions) {
        ss << prod.right.size() << ", ";
    }

    ss << "\n};\n\n";

    // 生成规约动作代码
    ss << "/* 执行规约动作 */\n";
    ss << "static void yy_reduce(int rule_num, int* top, YYSTYPE* stack, int* state_stack) {\n";
    ss << "  int symbols_to_pop = yyr2[rule_num];\n";
    ss << "  printf(\"  规约详情: 规则%d, 当前栈顶=%d, 弹出%d个符号\\n\", rule_num, *top, symbols_to_pop);\n";
    ss << "  YYSTYPE yyval;\n\n";

    ss << "  /* 计算栈中元素的位置, $1 是栈中第一个要规约的元素 */\n";
    ss << "  /* 对应关系: $1 = yyvsp[1], $2 = yyvsp[2], 以此类推 */\n";
    ss << "  YYSTYPE yyvsp[YYMAXDEPTH + 1]; // 临时数组，下标从1开始\n";
    ss << "  for (int i = 1; i <= symbols_to_pop; i++) {\n";
    ss << "    yyvsp[i] = stack[*top - symbols_to_pop + i];\n";
    ss << "  }\n\n";

    ss << "  /* 默认动作: 将$1的值赋给$$ */\n";
    ss << "  if (symbols_to_pop > 0) {\n";
    ss << "    yyval = yyvsp[1]; // $$ = $1\n";
    ss << "  }\n\n";

    ss << "  /* 根据规则执行语义动作 */\n";
    ss << "  printf(\"  执行语义动作: 规则%d\\n\", rule_num);\n";
    ss << "  switch(rule_num) {\n";

    // 生成每条规则的语义动作
    for (size_t i = 0; i < parser.productions.size(); ++i) {
        const Production& prod = parser.productions[i];

        ss << "    case " << i << ": /* " << prod.left.name << " -> ";
        if (prod.right.empty()) {
            ss << "ε";
        } else {
            for (const Symbol& sym : prod.right) {
                ss << sym.name << " ";
            }
        }

        ss << " */\n";

        // 如果有语义动作，处理并添加它
        if (!prod.semantic_action.empty()) {
            std::string processed_action = processSemanticAction(
                prod.semantic_action.substr(1, prod.semantic_action.length() - 2), // 去除大括号
                prod);
            ss << "      {\n"; // 添加开始花括号
            ss << "        " << processed_action << "\n";
            ss << "        printf(\"    完成语义动作: %s\\n\", \"" << prod.left.name << "\");\n";
            ss << "      }\n"; // 添加结束花括号
        }

        ss << "      break;\n";
    }

    ss << "  }\n\n";
    ss << "  /* 保存归约结果，主函数负责调整栈 */\n";
    ss << "  stack[*top - symbols_to_pop + 1] = yyval;\n";
    ss << "}\n\n";

    // 主解析函数
    ss << "/* 语法分析主函数 */\n";
    ss << "int yyparse(void) {\n";
    ss << "  int state = 0;\n";
    ss << "  int top = 0;\n";
    ss << "  int token_raw;\n";
    ss << "  int token;\n";
    ss << "  int action;\n";
    ss << "  YYSTYPE stack[YYMAXDEPTH];\n";
    ss << "  int state_stack[YYMAXDEPTH];\n\n"; // 添加状态栈

    ss << "  printf(\"====== 开始语法分析 ======\\n\");\n";
    ss << "  state_stack[0] = 0;\n";
    ss << "  token_raw = yylex();\n";
    ss << "  token = yytranslate_token(token_raw);\n";
    ss << "  printf(\"获取首个token: raw=%d, translated=%d\\n\", token_raw, token);\n\n";

    ss << "  while (1) {\n";
    ss << "    printf(\"当前状态: %d, token(raw)=%d, token(translated)=%d\\n\", state, token_raw, token);\n";
    ss << "    if (token == YYUNDEF) {\n";
    ss << "      printf(\"检测到未定义的token: %d\\n\", token_raw);\n";
    ss << "      yyerror(\"无法识别的终结符\");\n";
    ss << "      return 1;\n";
    ss << "    }\n\n";

    ss << "    action = yytable[state * YYNTOKENS + token];\n\n";
    ss << "    printf(\"查找动作: yytable[%d * %d + %d] = %d (raw token %d)\\n\", state, YYNTOKENS, token, action, token_raw);\n\n";

    ss << "    if (action == -32767) { /* 错误 */\n";
    ss << "      /* 收集期待的 token */\n";
    ss << "      const char* expected[YYNTOKENS];\n";
    ss << "      int expected_count = 0;\n";
    ss << "      for (int i = 0; i < YYNTOKENS; i++) {\n";
    ss << "        int test_action = yytable[state * YYNTOKENS + i];\n";
    ss << "        if (test_action != -32767) {\n";
    ss << "          expected[expected_count++] = yytname[i];\n";
    ss << "        }\n";
    ss << "      }\n";
    ss << "      \n";
    ss << "      /* 记录错误 */\n";
    ss << "      extern int yylineno;\n";
    ss << "      extern char* yytext;\n";
    ss << "      add_error(yylineno, \"syntax error, unexpected token\", yytext, expected, expected_count);\n";
    ss << "      \n";
    ss << "      /* 输出错误并退出 */\n";
    ss << "      print_errors_json();\n";
    ss << "      return 1;\n";
    ss << "    }\n\n";

    ss << "    if (action > 0) { /* 移入 */\n";
    ss << "      printf(\"执行移入操作: 状态%d -> 状态%d\\n\", state, action);\n";
    ss << "      stack[++top] = yylval;\n";
    ss << "      state_stack[top] = action;\n"; // 存储新状态
    ss << "      state = action;\n";
    ss << "      int next_raw = yylex();\n";
    ss << "      int next_token = yytranslate_token(next_raw);\n";
    ss << "      printf(\"获取下一个token: raw=%d\\n\", next_raw);\n";
    ss << "      printf(\"转换token结果: %d -> %d\\n\", next_raw, next_token);\n";
    ss << "      token_raw = next_raw;\n";
    ss << "      token = next_token;\n";
    ss << "    } else if (action < 0) { /* 规约 */\n";
    ss << "      int rule = -action - 1;\n";
    ss << "      printf(\"执行规约操作: 使用规则%d\\n\", rule);\n";
    ss << "      yy_reduce(rule, &top, stack, state_stack);\n";
    ss << "      printf(\"规约后的栈顶位置: %d\\n\", top);\n";

    ss << "      /* 弹出状态栈中的规约符号对应的状态 */\n";
    ss << "      int symbols_to_pop = yyr2[rule];\n";
    ss << "      top -= symbols_to_pop;\n"; // 先调整栈顶指针
    ss << "      printf(\"规约后的状态栈顶: %d, 当前状态: %d\\n\", top, state_stack[top]);\n";

    ss << "      /* 通过GOTO表确定新状态 */\n";
    ss << "      int nonterminal = yyr1[rule] - YYNTOKENS;\n";
    ss << "      int goto_index = state_stack[top] * YYNNTS + nonterminal;\n";
    ss << "      printf(\"GOTO表查询: 状态%d + 非终结符%d, 索引=%d\\n\", state_stack[top], nonterminal, goto_index);\n";

    // 添加安全检查
    ss << "      if (goto_index < 0 || goto_index >= " << (canonical_collection.size() * nonTerminals.size()) << ") {\n";
    ss << "        printf(\"错误: GOTO表索引越界! goto_index=%d\\n\", goto_index);\n";
    ss << "        yyerror(\"GOTO表索引错误\");\n";
    ss << "        return 3;\n";
    ss << "      }\n";

    ss << "      int next_state = yygoto[goto_index];\n";
    ss << "      printf(\"GOTO表结果: [%d][%d] = %d\\n\", state_stack[top], nonterminal, next_state);\n";

    ss << "      if (next_state == -1) {\n";
    ss << "        printf(\"错误: GOTO表中没有对应项! 状态%d, 非终结符%d\\n\", state_stack[top], nonterminal);\n";
    ss << "        yyerror(\"GOTO表错误\");\n";
    ss << "        return 2;\n";
    ss << "      }\n";

    ss << "      /* 将新状态压入栈 */\n";
    ss << "      state_stack[++top] = next_state;\n"; // 压入新状态
    ss << "      state = next_state;\n";
    ss << "      printf(\"规约后的新状态: %d\\n\", state);\n";
    ss << "    } else { /* 接受 */\n";
    ss << "      printf(\"接受输入, 分析成功完成!\\n\");\n";
    ss << "      if (error_count > 0) {\n";
    ss << "        print_errors_json();\n";
    ss << "        return 1;\n";
    ss << "      }\n";
    ss << "      return 0;\n";
    ss << "    }\n";
    ss << "    printf(\"--------------------\\n\");\n"; // 分隔不同的状态转换
    ss << "  }\n";
    ss << "  printf(\"====== 语法分析结束 ======\\n\");\n";
    ss << "  \n";
    ss << "  /* 如果有错误，输出 JSON */\n";
    ss << "  if (error_count > 0) {\n";
    ss << "    print_errors_json();\n";
    ss << "    return 1;\n";
    ss << "  }\n";
    ss << "  return 0;\n";
    ss << "}\n\n";

    // 添加用户代码
    if (!parser.program_code.empty()) {
        ss << "/* 用户代码 */\n";
        ss << parser.program_code << "\n";
    }

    return ss.str();
}

void LRGenerator::buildCanonicalCollection()
{
    transitions.clear();
    canonical_collection.clear();

    // 首先添加增广文法的起始项
    addAugmentedProduction();

    if (parser.productions.empty()) {
        std::cerr << "错误: 产生式列表为空!" << std::endl;
        return;
    }

    // 创建初始项集
    ItemSet initialItemSet;
    LRItem initialItem = {
        parser.productions[0],
        0,
        parser.getSymbol("$")
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

// 获取所有终结符（按照符号id排序）
std::vector<Symbol> LRGenerator::getSortedTerminals() const
{
    std::vector<Symbol> terminals;

    terminals.push_back(parser.getSymbol("$"));

    for (const auto& [name, symbol] : parser.symbol_table) {
        if (name == "$" || name == "ε") {
            continue;
        }
        if (symbol.type == ElementType::TOKEN || symbol.type == ElementType::LITERAL) {
            terminals.push_back(symbol);
        }
    }

    std::sort(terminals.begin() + 1, terminals.end(), [](const Symbol& a, const Symbol& b) {
        return a.id < b.id;
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

std::vector<int> LRGenerator::computeRawTokenValues(const std::vector<Symbol>& terminals) const
{
    std::vector<int> values(terminals.size(), 0);
    int nextTokenValue = 256;

    for (size_t i = 0; i < terminals.size(); ++i) {
        const Symbol& sym = terminals[i];

        if (i == 0) {
            values[i] = 0; // $ 对应 EOF
            continue;
        }

        if (sym.type == ElementType::LITERAL) {
            values[i] = parseLiteralTokenValue(sym.name);
        } else {
            values[i] = nextTokenValue++;
        }
    }

    return values;
}

int LRGenerator::parseLiteralTokenValue(const std::string& literal) const
{
    if (literal.size() < 2 || literal.front() != '\'' || literal.back() != '\'') {
        throw std::runtime_error("无效的字面量符号: " + literal);
    }

    std::string content = literal.substr(1, literal.size() - 2);
    if (content.empty()) {
        throw std::runtime_error("空的字面量符号: " + literal);
    }

    auto parseEscape = [](const std::string& str, size_t& pos) -> unsigned char {
        auto toHex = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') {
                return ch - '0';
            }
            if (ch >= 'a' && ch <= 'f') {
                return 10 + (ch - 'a');
            }
            if (ch >= 'A' && ch <= 'F') {
                return 10 + (ch - 'A');
            }
            return -1;
        };

        if (str[pos] != '\\') {
            return static_cast<unsigned char>(str[pos++]);
        }

        pos++; // 跳过反斜杠
        if (pos >= str.size()) {
            throw std::runtime_error("不完整的转义序列");
        }

        char esc = str[pos++];
        switch (esc) {
        case '\\':
            return '\\';
        case '\'':
            return '\'';
        case '"':
            return '"';
        case 'n':
            return '\n';
        case 't':
            return '\t';
        case 'r':
            return '\r';
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7': {
            int value = esc - '0';
            int count = 1;
            while (count < 3 && pos < str.size() && str[pos] >= '0' && str[pos] <= '7') {
                value = (value << 3) + (str[pos++] - '0');
                count++;
            }
            return static_cast<unsigned char>(value);
        }
        case 'x':
        case 'X': {
            int value = 0;
            int digits = 0;
            while (pos < str.size()) {
                int hex = toHex(str[pos]);
                if (hex < 0) {
                    break;
                }
                value = (value << 4) + hex;
                pos++;
                digits++;
            }
            if (digits == 0) {
                throw std::runtime_error("\\x 后缺少十六进制数字");
            }
            return static_cast<unsigned char>(value);
        }
        default:
            return static_cast<unsigned char>(esc);
        }
    };

    size_t pos = 0;
    int value = 0;
    while (pos < content.size()) {
        unsigned char ch = parseEscape(content, pos);
        value = (value << 8) | ch;
    }

    return value;
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

    std::vector<Symbol> terminals = getSortedTerminals();
    std::vector<Symbol> nonTerminals = getSortedNonTerminals();

    // 生成标题和基本信息
    ss << "# LR(1) 分析表\n\n";
    ss << "## 基本信息\n\n";
    ss << "- 状态数量: " << canonical_collection.size() << "\n";
    ss << "- 终结符数量: " << terminals.size() - 1 << " (不含 $)\n";

    int literalCount = 0;
    int namedTokenCount = 0;
    for (size_t i = 1; i < terminals.size(); ++i) {
        if (terminals[i].type == ElementType::LITERAL) {
            literalCount++;
        } else {
            namedTokenCount++;
        }
    }

    ss << "- 其中字面量数量: " << literalCount << "\n";
    ss << "- 其中Token数量: " << namedTokenCount << "\n";
    ss << "- 非终结符数量: " << nonTerminals.size() << "\n";
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

    // 生成ACTION表
    ss << "## ACTION表\n\n";

    // 收集有动作项的终结符
    std::vector<std::pair<int, Symbol>> usedTerminals;
    usedTerminals.push_back({ 0, terminals[0] }); // $符号总是包含的

    for (int i = 1; i < terminals.size(); ++i) {
        bool used = false;
        for (int state = 0; state < canonical_collection.size(); ++state) {
            if (action_table.find(state) != action_table.end() && action_table.at(state).find(terminals[i]) != action_table.at(state).end() && action_table.at(state).at(terminals[i]).type != ActionType::ERROR) {
                usedTerminals.push_back({ i, terminals[i] });
                used = true;
                break;
            }
        }
    }

    // 生成表头行1: 列序号
    ss << "| 编号 |";
    for (const auto& [index, _] : usedTerminals) {
        ss << " " << index << " |";
    }

    ss << "\n| --- |";
    // 表头分隔线
    for (size_t i = 0; i < usedTerminals.size(); ++i) {
        ss << " --- |";
    }
    ss << "\n";

    // 生成表头行2: 终结符名称
    ss << "| 状态 |";
    for (const auto& [_, term] : usedTerminals) {
        ss << " " << term.name << " |";
    }
    ss << "\n";

    // ACTION表内容
    for (int state = 0; state < canonical_collection.size(); ++state) {
        bool hasAction = false;
        std::stringstream rowss;
        rowss << "| " << state << " |";

        for (const auto& [index, term] : usedTerminals) {
            if (action_table.find(state) != action_table.end() && action_table.at(state).find(term) != action_table.at(state).end()) {

                const ActionEntry& entry = action_table.at(state).at(term);
                if (entry.type != ActionType::ERROR) {
                    rowss << " " << actionEntryToString(entry) << " |";
                    hasAction = true;
                } else {
                    rowss << " |";
                }
            } else {
                rowss << " |";
            }
        }

        // 只输出有动作的行
        if (hasAction) {
            ss << rowss.str() << "\n";
        }
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
    ss << "    YYEOF = 0,                     /* \"文件结束\" */\n";

    // 获取所有终结符并按名称排序
    std::vector<Symbol> terminals = getSortedTerminals();
    std::vector<int> rawValues = computeRawTokenValues(terminals);

    // 添加终结符令牌定义（不包括$和ε）
    for (size_t i = 1; i < terminals.size(); ++i) {
        const Symbol& terminal = terminals[i];
        if (terminal.name == "ε") {
            continue;
        }

        if (terminal.type == ElementType::TOKEN) {
            ss << "    " << terminal.name << " = " << rawValues[i] << ",\n";
        }
    }

    ss << "  };\n";
    ss << "#endif\n\n";

    // 添加令牌定义宏
    ss << "/* 令牌定义宏 */\n";
    ss << "#define YYEOF 0\n";

    for (size_t i = 1; i < terminals.size(); ++i) {
        const Symbol& terminal = terminals[i];
        if (terminal.type != ElementType::TOKEN || terminal.name == "ε") {
            continue;
        }
        ss << "#define " << terminal.name << " " << rawValues[i] << "\n";
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