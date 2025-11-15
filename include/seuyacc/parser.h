#ifndef SEUYACC_PARSER_H
#define SEUYACC_PARSER_H

#include "production.h"
#include "symbol.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace seuyacc {

// 语法文件的解析器
class YaccParser {
public:
    // 构造函数
    YaccParser()
        : current_precedence(0)
        , next_symbol_id(0)
    {
    }

    // 存储符号映射 (符号名 -> 符号对象)
    std::unordered_map<std::string, Symbol> symbol_table;

    // 存储语法规则
    std::vector<Production> productions;

    // 起始符号
    std::string start_symbol;

    // 声明部分的代码块
    std::string declaration_code;

    // union 代码块
    std::string union_code;

    // 程序部分的代码
    std::string program_code;

    // 解析Yacc文件的方法
    bool parseYaccFile(const std::string& filename);

    // 打印解析结果
    void printParsedInfo() const;

    // 获取或创建符号，确保已分配唯一id
    Symbol& ensureSymbol(const std::string& name, ElementType type);
    const Symbol& getSymbol(const std::string& name) const;

private:
    // 定义部分处理函数
    void parseTokenSection(const std::string& line);
    void parseStartSymbol(const std::string& line);
    bool parseDeclarationCode(std::ifstream& file);
    bool parseUnionCode(std::string& buffer, size_t& pos);
    void parseTypeDeclaration(const std::string& line);
    void parseAssociativity(const std::string& line, Associativity assoc);

    // 规则部分按字符处理的函数
    bool parseRulesSection(std::ifstream& file);
    bool parseRule(std::string& buffer, size_t& pos);
    bool parseRuleName(std::string& buffer, size_t& pos, std::string& rule_name);
    bool parseProductions(std::string& buffer, size_t& pos, const std::string& rule_name);
    bool parseProduction(std::string& buffer, size_t& pos, const std::string& rule_name);
    bool parseSymbol(std::string& buffer, size_t& pos, Symbol& symbol);
    bool parseSemanticAction(std::string& buffer, size_t& pos, std::string& action);

    // 程序部分处理函数
    bool parseProgramSection(std::ifstream& file);

    // 辅助函数
    void skipWhitespaceAndComments(std::string& buffer, size_t& pos);
    bool checkChar(std::string& buffer, size_t pos, char expected);
    void validateSymbols();
    void synchronizeProductionSymbols();
    const Symbol& resolveSymbol(const Symbol& symbol) const;

    // 当前最大优先级
    int current_precedence;
    int next_symbol_id;

    // 用于规则验证阶段
    std::unordered_map<std::string, Symbol> temp_symbols; // 存储解析规则阶段遇到的临时符号如字面量

    // 存储所有已知的非终结符
    std::unordered_map<std::string, Symbol> defined_non_terminals;
};

} // namespace seuyacc

#endif // SEUYACC_PARSER_H