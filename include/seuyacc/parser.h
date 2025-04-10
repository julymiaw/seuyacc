#ifndef SEUYACC_PARSER_H
#define SEUYACC_PARSER_H

#include "production.h"
#include "symbol.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace seuyacc {

// 定义解析器状态
enum class ParserState {
    EXPECTING_RULE_NAME, // 期望规则名
    EXPECTING_COLON, // 期望冒号
    IN_PRODUCTION, // 在产生式中
    END_OF_RULE // 规则结束
};

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
    void parseTokenSection(const std::string& line);
    void parseGrammarRules(const std::string& line);
    void parseStartSymbol(const std::string& line);
    void processProductionPart(const std::string& part);
    void parseProductionRightSide(const std::string& right_side, Production& prod);
    void validateNonTerminals();

    // 当前正在处理的规则
    std::string current_rule_name;
    ParserState parser_state = ParserState::EXPECTING_RULE_NAME;

    // 存储所有已知的非终结符
    std::unordered_set<std::string> non_terminals;
};

} // namespace seuyacc

#endif // SEUYACC_PARSER_H