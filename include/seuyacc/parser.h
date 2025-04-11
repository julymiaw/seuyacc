#ifndef SEUYACC_PARSER_H
#define SEUYACC_PARSER_H

#include "production.h"
#include "symbol.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace seuyacc {

// 语法文件的解析器
class YaccParser {
public:
    // 存储token声明
    std::vector<Symbol> tokens;

    // 存储词法单元映射
    std::unordered_map<std::string, int> token_map;

    // 存储语法规则
    std::vector<Production> productions;

    // 起始符号
    std::string start_symbol;

    // 解析Yacc文件的方法
    bool parseYaccFile(const std::string& filename);

    // 打印解析结果
    void printParsedInfo() const;

private:
    // 定义部分处理函数
    void parseTokenSection(const std::string& line);
    void parseStartSymbol(const std::string& line);

    // 规则部分按字符处理的函数
    bool parseRulesSection(std::ifstream& file);
    bool parseRule(std::string& buffer, size_t& pos);
    bool parseRuleName(std::string& buffer, size_t& pos, std::string& rule_name);
    bool parseProductions(std::string& buffer, size_t& pos, const std::string& rule_name);
    bool parseProduction(std::string& buffer, size_t& pos, const std::string& rule_name);
    bool parseSymbol(std::string& buffer, size_t& pos, Symbol& symbol);
    bool parseSemanticAction(std::string& buffer, size_t& pos, std::string& action);

    // 辅助函数
    void skipWhitespaceAndComments(std::string& buffer, size_t& pos);
    bool checkChar(std::string& buffer, size_t pos, char expected);
    void validateNonTerminals();

    // 当前正在处理的规则
    std::string current_rule_name;

    // 存储所有已知的非终结符
    std::unordered_set<std::string> non_terminals;
};

} // namespace seuyacc

#endif // SEUYACC_PARSER_H