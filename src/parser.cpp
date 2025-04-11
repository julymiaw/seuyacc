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
            // 检查是否为 %union 块
            if (cleaned_line.substr(0, 6) == "%union") {
                std::string buffer = cleaned_line.substr(7) + "\n";
                // 读取更多行直到找到 union 块的结束
                std::string union_line;
                while (std::getline(file, union_line)) {
                    buffer += union_line + "\n";
                    // 匹配花括号的结束
                    if (union_line.find('}') != std::string::npos) {
                        break;
                    }
                }
                size_t pos = 0;
                if (!parseUnionCode(buffer, pos)) {
                    return false;
                }
                continue;
            }
            // 处理定义部分的其他指令
            if (cleaned_line.substr(0, 6) == "%token") {
                parseTokenSection(cleaned_line);
            } else if (cleaned_line.substr(0, 6) == "%start") {
                parseStartSymbol(cleaned_line);
            } else if (cleaned_line.substr(0, 5) == "%type") {
                parseTypeDeclaration(cleaned_line);
            } else if (cleaned_line.substr(0, 5) == "%left") {
                parseAssociativity(cleaned_line, Associativity::LEFT);
            } else if (cleaned_line.substr(0, 6) == "%right") {
                parseAssociativity(cleaned_line, Associativity::RIGHT);
            } else if (cleaned_line.substr(0, 9) == "%nonassoc") {
                parseAssociativity(cleaned_line, Associativity::NONASSOC);
            }
        }
    }

    // 验证非终结符
    validateNonTerminals();
    return true;
}

void YaccParser::parseTokenSection(const std::string& line)
{
    std::string type_name = "";
    std::string tokens_str = "";

    // 匹配带类型的 token 定义: %token<type> ...
    std::regex token_type_pattern("%token\\s*<([^>]+)>\\s*(.+)");
    std::smatch type_matches;
    if (std::regex_search(line, type_matches, token_type_pattern)) {
        type_name = type_matches[1];
        tokens_str = type_matches[2];
    } else {
        // 匹配普通 token 定义: %token ...
        std::regex token_pattern("%token\\s+(.+)");
        std::smatch matches;
        if (std::regex_search(line, matches, token_pattern)) {
            tokens_str = matches[1];
        }
    }

    if (!tokens_str.empty()) {
        std::istringstream iss(tokens_str);
        std::string token;

        while (iss >> token) {
            Symbol sym;
            sym.name = token;
            sym.type = ElementType::TOKEN;
            if (!type_name.empty()) {
                sym.value_type = type_name;
            }

            tokens.push_back(sym);
            token_map[token] = tokens.size() - 1;
            symbol_table[token] = sym;
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

// 解析 Union 代码块
bool YaccParser::parseUnionCode(std::string& buffer, size_t& pos)
{
    // 跳过可能的空白字符
    skipWhitespaceAndComments(buffer, pos);

    // 查找开始的花括号
    size_t brace_start = buffer.find('{', pos);
    if (brace_start == std::string::npos) {
        std::cerr << "错误: %union 缺少开始花括号 '{'" << std::endl;
        return false;
    }

    int brace_count = 1; // 已找到一个左花括号
    pos = brace_start + 1; // 从左花括号的下一个位置开始

    // 使用与parseSemanticAction相似的逻辑处理嵌套括号、引号和注释
    char quote_type = 0;
    bool escaped = false;
    bool in_line_comment = false;
    bool in_block_comment = false;
    std::string union_content = "{"; // 包含开始的花括号

    while (pos < buffer.size() && brace_count > 0) {
        char c = buffer[pos];
        char next_c = (pos + 1 < buffer.size()) ? buffer[pos + 1] : '\0';
        union_content += c; // 添加当前字符到union内容

        if (escaped) {
            escaped = false;
        } else if (c == '\\' && !in_line_comment && !in_block_comment) {
            escaped = true;
        } else if (in_line_comment) {
            if (c == '\n') {
                in_line_comment = false;
            }
        } else if (in_block_comment) {
            if (c == '*' && next_c == '/') {
                in_block_comment = false;
                union_content += next_c; // 添加'/'到union内容
                pos++;
            }
        } else if (quote_type == 0 && c == '/' && next_c == '/') {
            in_line_comment = true;
            union_content += next_c; // 添加第二个'/'到union内容
            pos++;
        } else if (quote_type == 0 && c == '/' && next_c == '*') {
            in_block_comment = true;
            union_content += next_c; // 添加'*'到union内容
            pos++;
        } else if (quote_type == 0 && !in_line_comment && !in_block_comment) {
            if (c == '\'' || c == '\"') {
                quote_type = c;
            } else if (c == '{') {
                brace_count++;
            } else if (c == '}') {
                brace_count--;
            }
        } else if (!in_line_comment && !in_block_comment && c == quote_type) {
            quote_type = 0;
        }

        pos++;
    }

    if (brace_count != 0) {
        std::cerr << "错误: %union 缺少结束花括号 '}'" << std::endl;
        return false;
    }

    union_code = union_content;
    return true;
}

// 解析类型声明
void YaccParser::parseTypeDeclaration(const std::string& line)
{
    // 匹配 %type<类型>
    std::regex type_pattern("%type\\s*<([^>]+)>\\s*(.+)");
    std::smatch matches;

    if (std::regex_search(line, matches, type_pattern)) {
        std::string type_name = matches[1];
        std::string symbols_str = matches[2];

        // 分割符号列表
        std::istringstream symbols_stream(symbols_str);
        std::string symbol;
        while (symbols_stream >> symbol) {
            // 将符号与类型关联
            type_map[symbol] = type_name;

            // 如果符号已存在于符号表中，更新其类型
            if (symbol_table.find(symbol) != symbol_table.end()) {
                symbol_table[symbol].value_type = type_name;
            }
        }
    }
}

// 解析结合性和优先级声明
void YaccParser::parseAssociativity(const std::string& line, Associativity assoc)
{
    // 首先确保没有注释干扰
    std::string cleaned_line = std::regex_replace(line, std::regex("//.*$"), "");
    cleaned_line = std::regex_replace(cleaned_line, std::regex("/\\*.*?\\*/"), ""); // 简单处理块注释

    // 匹配 %left/%right/%nonassoc 后的符号
    std::regex assoc_pattern("%(left|right|nonassoc)\\s+(.+)");
    std::smatch matches;

    if (std::regex_search(cleaned_line, matches, assoc_pattern)) {
        std::string symbols_str = matches[2];

        // 增加优先级计数
        current_precedence++;

        // 分割符号列表
        std::istringstream symbols_stream(symbols_str);
        std::string symbol;
        while (symbols_stream >> symbol) {
            // 检查符号是否带有类型声明
            std::string symbol_name = symbol;
            std::string type_name = "";

            // 处理带类型的符号，如 <type>symbol
            std::regex typed_symbol_pattern("<([^>]+)>\\s*([^\\s]+)");
            std::smatch type_matches;
            if (std::regex_search(symbol, type_matches, typed_symbol_pattern)) {
                type_name = type_matches[1];
                symbol_name = type_matches[2];
            }

            // 更新或添加符号到符号表
            Symbol sym;
            sym.name = symbol_name;
            sym.type = ElementType::TOKEN; // 操作符通常是终结符
            sym.precedence = current_precedence;
            sym.assoc = assoc;
            if (!type_name.empty()) {
                sym.value_type = type_name;
            }

            // 添加到符号表
            symbol_table[symbol_name] = sym;

            // 添加到优先级映射表
            precedence_map[current_precedence].insert(symbol_name);
        }
    }
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

    // 改进 Tokens 输出格式
    std::cout << "\nTokens (" << tokens.size() << "):" << std::endl;
    for (const auto& token : tokens) {
        std::cout << "  " << token.name;
        if (!token.value_type.empty()) {
            std::cout << " <" << token.value_type << ">";
        }

        // 添加优先级信息
        if (token.precedence > 0) {
            std::cout << " [优先级:" << token.precedence;

            // 添加结合性信息
            switch (token.assoc) {
            case Associativity::LEFT:
                std::cout << ", 左结合";
                break;
            case Associativity::RIGHT:
                std::cout << ", 右结合";
                break;
            case Associativity::NONASSOC:
                std::cout << ", 无结合";
                break;
            default:
                break;
            }
            std::cout << "]";
        }

        std::cout << std::endl;
    }

    // 输出所有非终结符及其类型
    std::cout << "\n非终结符:" << std::endl;
    std::set<std::string> printed_non_terminals;

    // 首先添加起始符号
    if (!start_symbol.empty()) {
        printed_non_terminals.insert(start_symbol);
        std::cout << "  " << start_symbol;
        if (type_map.find(start_symbol) != type_map.end()) {
            std::cout << " <" << type_map.at(start_symbol) << ">";
        }
        std::cout << std::endl;
    }

    // 添加其他非终结符
    for (const auto& prod : productions) {
        if (printed_non_terminals.find(prod.left.name) == printed_non_terminals.end()) {
            printed_non_terminals.insert(prod.left.name);
            std::cout << "  " << prod.left.name;
            if (!prod.left.value_type.empty()) {
                std::cout << " <" << prod.left.value_type << ">";
            } else if (type_map.find(prod.left.name) != type_map.end()) {
                std::cout << " <" << type_map.at(prod.left.name) << ">";
            }
            std::cout << std::endl;
        }
    }

    // 改进优先级和结合性信息输出
    std::cout << "\n优先级和结合性信息:" << std::endl;
    for (const auto& [prec, symbols] : precedence_map) {
        std::cout << "  优先级 " << prec << ": ";

        // 找出该优先级的结合性
        Associativity assoc = Associativity::NONE;
        std::string assoc_str = "无";

        if (!symbols.empty()) {
            const std::string& first_symbol = *symbols.begin();
            if (symbol_table.find(first_symbol) != symbol_table.end()) {
                assoc = symbol_table.at(first_symbol).assoc;
                switch (assoc) {
                case Associativity::LEFT:
                    assoc_str = "左";
                    break;
                case Associativity::RIGHT:
                    assoc_str = "右";
                    break;
                case Associativity::NONASSOC:
                    assoc_str = "无";
                    break;
                default:
                    assoc_str = "未知";
                }
            }
        }

        std::cout << "[" << assoc_str << "结合] ";

        // 输出符号
        for (const auto& symbol : symbols) {
            std::cout << symbol << " ";
        }
        std::cout << std::endl;
    }

    // 输出union定义
    if (!union_code.empty()) {
        std::cout << "\n\nUnion 定义:\n"
                  << union_code << std::endl;
    }

    if (!declaration_code.empty()) {
        std::cout << "\n\n声明代码块:\n"
                  << declaration_code << std::endl;
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

        // 添加类型信息
        if (!prod.left.value_type.empty()) {
            std::cout << ", " << prod.left.value_type;
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

            // 添加类型信息
            if (!sym.value_type.empty()) {
                std::cout << ", " << sym.value_type;
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