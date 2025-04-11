#include "seuyacc/parser.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace seuyacc {

bool YaccParser::parseYaccFile(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "无法打开文件: " << filename << std::endl;
        return false;
    }

    std::string line;
    bool in_definitions = true;
    bool in_rules = false;
    bool in_code = false;

    while (std::getline(file, line)) {
        // 检查是否为代码块开始
        if (line == "%{" && in_definitions) {
            if (!parseDeclarationCode(file)) {
                return false;
            }
            continue;
        }

        // 去除注释和尾部空白
        std::string cleaned_line = std::regex_replace(line, std::regex("//.*$"), "");
        cleaned_line = std::regex_replace(cleaned_line, std::regex("^\\s+|\\s+$"), "");

        if (cleaned_line.empty())
            continue;

        // 识别各个section
        if (cleaned_line == "%%") {
            if (in_definitions) {
                in_definitions = false;
                in_rules = true;

                // 当进入规则部分时，切换到按字符处理的模式
                if (!parseRulesSection(file)) {
                    return false;
                }

                // parseRulesSection 已经处理到了第二个 %% 或文件末尾
                in_rules = false;
                in_code = true;

                // 处理程序部分
                if (!parseProgramSection(file)) {
                    return false;
                }
            }
            continue;
        }

        if (in_definitions) {
            // 处理定义部分
            if (cleaned_line.substr(0, 6) == "%token") {
                parseTokenSection(cleaned_line);
            } else if (cleaned_line.substr(0, 6) == "%start") {
                parseStartSymbol(cleaned_line);
            }
        }
    }

    // 验证非终结符
    validateNonTerminals();
    return true;
}

void YaccParser::parseTokenSection(const std::string& line)
{
    std::regex token_pattern("%token\\s+(.+)");
    std::smatch matches;

    if (std::regex_search(line, matches, token_pattern)) {
        std::string token_list = matches[1];
        std::istringstream iss(token_list);
        std::string token;

        while (iss >> token) {
            Symbol sym;
            sym.name = token;
            sym.type = ElementType::TOKEN;
            tokens.push_back(sym);
            token_map[token] = tokens.size() - 1;
        }
    }
}

void YaccParser::parseStartSymbol(const std::string& line)
{
    std::regex start_pattern("%start\\s+(\\w+)");
    std::smatch matches;

    if (std::regex_search(line, matches, start_pattern)) {
        start_symbol = matches[1];
    }
}

bool YaccParser::parseDeclarationCode(std::ifstream& file)
{
    std::string line;

    while (std::getline(file, line)) {
        if (line == "%}") {
            return true;
        }
        declaration_code += line + "\n";
    }

    std::cerr << "错误: 未找到代码块结束标记 %}" << std::endl;
    return false;
}

bool YaccParser::parseRulesSection(std::ifstream& file)
{
    // 读取整个规则部分到一个字符串缓冲区
    std::string buffer;
    std::string line;
    bool found_end = false;

    while (std::getline(file, line)) {
        // 检查是否到达规则部分的结束
        if (line == "%%") {
            found_end = true;
            break;
        }
        buffer += line + "\n";
    }

    // 解析整个规则部分
    size_t pos = 0;
    skipWhitespaceAndComments(buffer, pos);

    while (pos < buffer.size()) {
        if (!parseRule(buffer, pos)) {
            return false;
        }
        skipWhitespaceAndComments(buffer, pos);
    }

    return found_end;
}

bool YaccParser::parseRule(std::string& buffer, size_t& pos)
{
    std::string rule_name;

    // 解析规则名
    if (!parseRuleName(buffer, pos, rule_name)) {
        return false;
    }

    // 添加规则名到非终结符集合
    non_terminals.insert(rule_name);

    // 跳过空白和注释
    skipWhitespaceAndComments(buffer, pos);

    // 检查冒号
    if (!checkChar(buffer, pos, ':')) {
        std::cerr << "错误: 预期找到冒号" << std::endl;
        return false;
    }
    pos++; // 跳过冒号

    // 解析产生式
    if (!parseProductions(buffer, pos, rule_name)) {
        return false;
    }

    return true;
}

bool YaccParser::parseRuleName(std::string& buffer, size_t& pos, std::string& rule_name)
{
    rule_name.clear();

    // 规则名必须是标识符
    if (pos < buffer.size() && (std::isalpha(buffer[pos]) || buffer[pos] == '_')) {
        rule_name += buffer[pos++];

        while (pos < buffer.size() && (std::isalnum(buffer[pos]) || buffer[pos] == '_')) {
            rule_name += buffer[pos++];
        }

        return true;
    }

    std::cerr << "错误: 预期找到规则名" << std::endl;
    return false;
}

bool YaccParser::parseProductions(std::string& buffer, size_t& pos, const std::string& rule_name)
{
    // 解析第一个产生式
    if (!parseProduction(buffer, pos, rule_name)) {
        return false;
    }

    skipWhitespaceAndComments(buffer, pos);

    // 解析可能的后续产生式（由 | 分隔）
    while (pos < buffer.size() && buffer[pos] == '|') {
        pos++; // 跳过 |
        skipWhitespaceAndComments(buffer, pos);

        if (!parseProduction(buffer, pos, rule_name)) {
            return false;
        }

        skipWhitespaceAndComments(buffer, pos);
    }

    // 检查分号
    if (!checkChar(buffer, pos, ';')) {
        std::cerr << "错误: 预期找到分号" << std::endl;
        return false;
    }
    pos++; // 跳过分号

    return true;
}

bool YaccParser::parseProduction(std::string& buffer, size_t& pos, const std::string& rule_name)
{
    Production prod;

    // 设置产生式左部
    Symbol left_sym;
    left_sym.name = rule_name;
    left_sym.type = ElementType::NON_TERMINAL;
    prod.left = left_sym;

    // 跳过空白和注释
    skipWhitespaceAndComments(buffer, pos);

    // 检查是否是空产生式 - 直接遇到分号或竖线
    if (pos < buffer.size() && (buffer[pos] == ';' || buffer[pos] == '|')) {
        // 这是一个空产生式，right 部分为空
        productions.push_back(prod);
        return true;
    }

    // 解析产生式右部
    while (pos < buffer.size()) {
        skipWhitespaceAndComments(buffer, pos);

        // 检查是否到达产生式结束
        if (pos >= buffer.size() || buffer[pos] == ';' || buffer[pos] == '|') {
            break;
        }

        // 检查是否为语义动作
        if (buffer[pos] == '{') {
            std::string action;
            if (!parseSemanticAction(buffer, pos, action)) {
                return false;
            }
            prod.semantic_action = action;
            // 假设语义动作必须在产生式的最后
            break;
        }

        // 解析符号
        Symbol symbol;
        if (!parseSymbol(buffer, pos, symbol)) {
            return false;
        }
        prod.right.push_back(symbol);
    }

    // 添加到产生式列表
    productions.push_back(prod);
    return true;
}

bool YaccParser::parseSymbol(std::string& buffer, size_t& pos, Symbol& symbol)
{
    skipWhitespaceAndComments(buffer, pos);

    if (pos >= buffer.size()) {
        return false;
    }

    // 检查是否为字面量（单引号包围）
    if (buffer[pos] == '\'') {
        size_t start_pos = pos;
        pos++; // 跳过开始的单引号

        bool escaped = false;
        while (pos < buffer.size()) {
            char c = buffer[pos];

            if (c == '\\' && !escaped) {
                escaped = true;
            } else if (c == '\'' && !escaped) {
                // 找到匹配的结束单引号
                pos++; // 跳过结束单引号
                symbol.name = buffer.substr(start_pos, pos - start_pos);
                symbol.type = ElementType::LITERAL;
                return true;
            } else {
                escaped = false;
            }

            pos++;
        }

        std::cerr << "错误: 未闭合的字面量" << std::endl;
        return false;
    }

    // 解析标识符
    if (std::isalpha(buffer[pos]) || buffer[pos] == '_') {
        std::string identifier;

        while (pos < buffer.size() && (std::isalnum(buffer[pos]) || buffer[pos] == '_')) {
            identifier += buffer[pos++];
        }

        symbol.name = identifier;

        // 判断符号类型
        if (token_map.find(identifier) != token_map.end()) {
            symbol.type = ElementType::TOKEN;
        } else {
            symbol.type = ElementType::NON_TERMINAL;
        }

        return true;
    }

    std::cerr << "错误: 无效的符号" << std::endl;
    return false;
}

bool YaccParser::parseSemanticAction(std::string& buffer, size_t& pos, std::string& action)
{
    if (buffer[pos] != '{') {
        std::cerr << "错误: 语义动作必须以 '{' 开始" << std::endl;
        return false;
    }

    size_t start_pos = pos;
    int brace_count = 1;
    pos++; // 跳过开始的 {

    char quote_type = 0; // 0表示不在引号内，'\''表示单引号，'\"'表示双引号
    bool escaped = false; // 是否为转义字符
    bool in_line_comment = false; // 是否在行注释中
    bool in_block_comment = false; // 是否在块注释中

    while (pos < buffer.size()) {
        char c = buffer[pos];
        char next_c = (pos + 1 < buffer.size()) ? buffer[pos + 1] : '\0';

        if (escaped) {
            // 如果是转义字符，下一个字符不做特殊处理
            escaped = false;
        } else if (c == '\\' && !in_line_comment && !in_block_comment) {
            // 标记转义字符（仅当不在注释中时）
            escaped = true;
        } else if (in_line_comment) {
            // 在行注释中，遇到换行符则结束注释
            if (c == '\n') {
                in_line_comment = false;
            }
        } else if (in_block_comment) {
            // 在块注释中，检查是否结束块注释
            if (c == '*' && next_c == '/') {
                in_block_comment = false;
                pos++; // 跳过 '/'
            }
        } else if (quote_type == 0 && c == '/' && next_c == '/') {
            // 不在引号内，开始行注释
            in_line_comment = true;
            pos++; // 跳过第二个 '/'
        } else if (quote_type == 0 && c == '/' && next_c == '*') {
            // 不在引号内，开始块注释
            in_block_comment = true;
            pos++; // 跳过 '*'
        } else if (quote_type == 0 && !in_line_comment && !in_block_comment) {
            // 不在引号或注释内时，检查是否遇到引号或花括号
            if (c == '\'' || c == '\"') {
                // 记录当前引号类型
                quote_type = c;
            } else if (c == '{') {
                brace_count++;
            } else if (c == '}') {
                brace_count--;
                if (brace_count == 0) {
                    // 找到匹配的结束花括号
                    pos++; // 跳过结束花括号
                    action = buffer.substr(start_pos, pos - start_pos);
                    return true;
                }
            }
        } else if (!in_line_comment && !in_block_comment && c == quote_type) {
            // 如果不在注释中且遇到与当前引号类型相同的字符，结束引号状态
            quote_type = 0;
        }

        pos++;
    }

    std::cerr << "错误: 未闭合的语义动作" << std::endl;
    return false;
}

bool YaccParser::parseProgramSection(std::ifstream& file)
{
    program_code.clear();
    std::string line;

    while (std::getline(file, line)) {
        program_code += line + "\n";
    }

    return true;
}

void YaccParser::skipWhitespaceAndComments(std::string& buffer, size_t& pos)
{
    while (pos < buffer.size()) {
        // 跳过空白字符
        while (pos < buffer.size() && std::isspace(buffer[pos])) {
            pos++;
        }

        // 如果遇到行注释
        if (pos + 1 < buffer.size() && buffer[pos] == '/' && buffer[pos + 1] == '/') {
            // 跳到行尾
            pos += 2;
            while (pos < buffer.size() && buffer[pos] != '\n') {
                pos++;
            }
            if (pos < buffer.size()) {
                pos++; // 跳过换行符
            }
            continue;
        }

        // 如果遇到块注释
        if (pos + 1 < buffer.size() && buffer[pos] == '/' && buffer[pos + 1] == '*') {
            // 跳到块注释结束
            pos += 2; // 跳过 /*
            while (pos + 1 < buffer.size() && !(buffer[pos] == '*' && buffer[pos + 1] == '/')) {
                pos++;
            }
            if (pos + 1 < buffer.size()) {
                pos += 2; // 跳过 */
            }
            continue;
        }

        break; // 如果没有更多的空白或注释，退出循环
    }
}

bool YaccParser::checkChar(std::string& buffer, size_t pos, char expected)
{
    return (pos < buffer.size() && buffer[pos] == expected);
}

void YaccParser::validateNonTerminals()
{
    // 遍历所有产生式
    for (const auto& prod : productions) {
        // 检查产生式右部的非终结符是否都在左部出现过
        for (const auto& symbol : prod.right) {
            if (symbol.type == ElementType::NON_TERMINAL) {
                // 检查该非终结符是否在左部出现过
                if (non_terminals.find(symbol.name) == non_terminals.end()) {
                    std::cerr << "错误: 非终结符 '" << symbol.name << "' 在右部出现但未在任何左部定义" << std::endl;
                    throw std::runtime_error("非终结符验证失败");
                }
            }
        }
    }
}

void YaccParser::printParsedInfo() const
{
    std::cout << "起始符号: " << start_symbol << std::endl;

    std::cout << "\nTokens (" << tokens.size() << "):" << std::endl;
    for (const auto& token : tokens) {
        std::cout << token.name << " ";
    }

    if (!declaration_code.empty()) {
        std::cout << "\n\n声明代码块:\n"
                  << declaration_code;
    }

    std::cout << "\n\n产生式规则 (" << productions.size() << "):" << std::endl;
    for (const auto& prod : productions) {
        std::cout << prod.left.name << " [";
        // 可以添加类型信息
        switch (prod.left.type) {
        case ElementType::TOKEN:
            std::cout << "TOKEN";
            break;
        case ElementType::NON_TERMINAL:
            std::cout << "NON_TERM";
            break;
        case ElementType::LITERAL:
            std::cout << "LIT";
            break;
        }
        std::cout << "] -> ";

        for (const auto& sym : prod.right) {
            std::cout << sym.name << " [";
            switch (sym.type) {
            case ElementType::TOKEN:
                std::cout << "TOKEN";
                break;
            case ElementType::NON_TERMINAL:
                std::cout << "NON_TERM";
                break;
            case ElementType::LITERAL:
                std::cout << "LIT";
                break;
            }
            std::cout << "] ";
        }

        if (!prod.semantic_action.empty()) {
            std::cout << prod.semantic_action;
        }

        std::cout << std::endl;
    }

    if (!program_code.empty()) {
        std::cout << "\n\n程序代码:\n"
                  << program_code;
    }
}

} // namespace seuyacc