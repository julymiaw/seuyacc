#ifndef SEUYACC_LR_GENERATOR_H
#define SEUYACC_LR_GENERATOR_H

#include "lr_item.h"
#include "parser.h"
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace seuyacc {

// 动作类型
enum class ActionType {
    SHIFT,
    REDUCE,
    ACCEPT,
    ERROR
};

// 动作表项
struct ActionEntry {
    ActionType type;
    int value; // 移入状态或规约产生式索引
};

// LR(1)分析表生成器
class LRGenerator {
public:
    // 构造函数，接收解析后的文法
    LRGenerator(const YaccParser& parser);

    // 生成LR(1)分析表
    void generateTable();

    // 将自动机转换为PlantUML格式
    std::string toPlantUML() const;

    // 获取生成的分析表
    const std::map<int, std::map<Symbol, ActionEntry>>& getActionTable() const
    {
        return action_table;
    }

    const std::map<int, std::map<Symbol, int>>& getGotoTable() const
    {
        return goto_table;
    }

    // 将ACTION和GOTO表导出为Markdown格式
    std::string toMarkdownTable() const;

private:
    // 辅助方法：将ActionEntry转换为可读字符串
    std::string actionEntryToString(const ActionEntry& entry) const;

    // 辅助方法：获取所有终结符（按名称排序）
    std::vector<Symbol> getSortedTerminals() const;

    // 辅助方法：获取所有非终结符（按名称排序）
    std::vector<Symbol> getSortedNonTerminals() const;

    // 辅助方法：找到产生式的字符串表示
    std::string getProductionString(int index) const;

    // 计算文法符号的FIRST集
    std::unordered_set<Symbol, SymbolHasher> computeFirst(const Symbol& symbol);
    std::unordered_set<Symbol, SymbolHasher> computeFirstOfSequence(const std::vector<Symbol>& sequence);

    // 计算项集的闭包
    ItemSet computeClosure(const ItemSet& itemSet);

    // 计算GOTO函数
    ItemSet computeGoto(const ItemSet& itemSet, const Symbol& symbol);

    // 构建项集规范族
    void buildCanonicalCollection();

    // 从项集规范族构建ACTION和GOTO表
    void buildActionGotoTable();

    // 添加增广文法的起始产生式
    void addAugmentedProduction();

    // 解析后的文法
    YaccParser parser;

    // 项集规范族
    std::vector<ItemSet> canonical_collection;

    // 状态转移
    std::vector<StateTransition> transitions;

    // ACTION表和GOTO表
    std::map<int, std::map<Symbol, ActionEntry>> action_table;
    std::map<int, std::map<Symbol, int>> goto_table;

    // 缓存FIRST集计算结果
    std::unordered_map<Symbol, std::unordered_set<Symbol, SymbolHasher>, SymbolHasher> first_cache;
};

} // namespace seuyacc

#endif // SEUYACC_LR_GENERATOR_H