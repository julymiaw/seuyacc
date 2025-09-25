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

    // 为每个产生式分配唯一id
    for (size_t i = 0; i < productions.size(); ++i) {
        productions[i].id = static_cast<int>(i);
    }
    // 验证符号
    validateSymbols();
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
            // 如果符号已存在于符号表中，更新其类型
            if (symbol_table.find(symbol) != symbol_table.end()) {
                symbol_table[symbol].value_type = type_name;
            } else {
                Symbol non_term_sym;
                non_term_sym.name = symbol;
                non_term_sym.type = ElementType::NON_TERMINAL;
                non_term_sym.value_type = type_name;
                symbol_table[symbol] = non_term_sym;
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

        // 分割符号列表，特殊处理单引号包裹的字面量
        size_t pos = 0;
        while (pos < symbols_str.size()) {
            // 跳过空白
            while (pos < symbols_str.size() && std::isspace(symbols_str[pos])) {
                pos++;
            }
            if (pos >= symbols_str.size())
                break;

            std::string symbol_name;
            std::string type_name = "";
            bool is_literal = false;

            // 检查是否为字面量（单引号包裹）
            if (symbols_str[pos] == '\'') {
                is_literal = true;
                size_t start_pos = pos;
                pos++; // 跳过开始的单引号

                bool escaped = false;
                while (pos < symbols_str.size()) {
                    char c = symbols_str[pos];
                    if (c == '\\' && !escaped) {
                        escaped = true;
                    } else if (c == '\'' && !escaped) {
                        // 找到匹配的结束单引号
                        pos++; // 跳过结束单引号
                        symbol_name = symbols_str.substr(start_pos, pos - start_pos);
                        break;
                    } else {
                        escaped = false;
                    }
                    pos++;
                }
                if (pos > symbols_str.size() || symbol_name.empty()) {
                    std::cerr << "错误: 未闭合的字面量" << std::endl;
                    continue;
                }
            } else {
                // 处理普通符号或带类型的符号
                std::string full_symbol;
                // 读取完整符号（包括可能的类型声明）
                while (pos < symbols_str.size() && !std::isspace(symbols_str[pos])) {
                    full_symbol += symbols_str[pos++];
                }

                // 处理带类型的符号，如 <type>symbol
                std::regex typed_symbol_pattern("<([^>]+)>\\s*([^\\s]+)");
                std::smatch type_matches;
                if (std::regex_search(full_symbol, type_matches, typed_symbol_pattern)) {
                    type_name = type_matches[1];
                    symbol_name = type_matches[2];
                } else {
                    symbol_name = full_symbol;
                }
            }

            // 检查符号表中是否已存在该符号
            if (symbol_table.find(symbol_name) != symbol_table.end()) {
                // 如果符号已存在，更新其优先级和结合性
                Symbol& existing_sym = symbol_table[symbol_name];

                // 如果是字面量，保持其LITERAL类型，否则设为TOKEN
                if (!is_literal && existing_sym.type == ElementType::NON_TERMINAL) {
                    existing_sym.type = ElementType::TOKEN;
                }

                // 设置优先级和结合性
                existing_sym.precedence = current_precedence;
                existing_sym.assoc = assoc;

                // 仅当当前符号指定了类型且原符号没有类型时才更新类型
                if (!type_name.empty() && existing_sym.value_type.empty()) {
                    existing_sym.value_type = type_name;
                }
            } else {
                // 符号不存在，创建新的符号
                Symbol sym;
                sym.name = symbol_name;
                sym.type = is_literal ? ElementType::LITERAL : ElementType::TOKEN;
                sym.precedence = current_precedence;
                sym.assoc = assoc;
                if (!type_name.empty()) {
                    sym.value_type = type_name;
                }

                // 添加到符号表
                symbol_table[symbol_name] = sym;
            }
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
    prod.precedence = 0; // 默认没有优先级

    // 设置产生式左部
    Symbol left_sym;
    left_sym.name = rule_name;
    left_sym.type = ElementType::NON_TERMINAL;

    // 如果在声明部分定义了属性，复制这些属性
    if (symbol_table.find(rule_name) != symbol_table.end()) {
        if (symbol_table[rule_name].type == ElementType::TOKEN) {
            std::cerr << "错误: Token '" << rule_name << "' 不能作为产生式左部" << std::endl;
            return false;
        }

        // 复制声明部分的类型信息，但保证是非终结符
        left_sym.value_type = symbol_table[rule_name].value_type;
    }

    // 将非终结符记录到集合中
    defined_non_terminals[rule_name] = left_sym;

    prod.left = left_sym;

    // 跳过空白和注释
    skipWhitespaceAndComments(buffer, pos);

    // 检查是否是空产生式 - 直接遇到分号或竖线
    if (pos < buffer.size() && (buffer[pos] == ';' || buffer[pos] == '|')) {
        // 这是一个空产生式，right 部分为空
        productions.push_back(prod);
        return true;
    }

    // 解析产生式右部的所有符号
    while (pos < buffer.size()) {
        skipWhitespaceAndComments(buffer, pos);

        // 检查是否到达产生式结束
        if (pos >= buffer.size() || buffer[pos] == ';' || buffer[pos] == '|') {
            break;
        }

        // 检查是否为符号
        if (buffer[pos] != '{') {
            // 解析符号
            Symbol symbol;
            if (!parseSymbol(buffer, pos, symbol)) {
                return false;
            }
            prod.right.push_back(symbol);

            // 更新产生式的优先级为右部最右边的终结符的优先级
            if (symbol.precedence > 0) {
                prod.precedence = symbol.precedence;
            }
        } else {
            // 遇到语义动作，确保它是最后一个元素
            std::string action;
            if (!parseSemanticAction(buffer, pos, action)) {
                return false;
            }
            prod.semantic_action = action;

            // 跳过可能的空白和注释
            skipWhitespaceAndComments(buffer, pos);

            // 确保语义动作后只能是产生式结束符号(; 或 |)
            if (pos < buffer.size() && buffer[pos] != ';' && buffer[pos] != '|') {
                std::cerr << "错误: 语义动作只能出现在产生式的最右侧" << std::endl;
                return false;
            }

            // 已经遇到语义动作，结束产生式右部解析
            break;
        }
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
                std::string literal_name = buffer.substr(start_pos, pos - start_pos);
                symbol.name = literal_name;
                symbol.type = ElementType::LITERAL;

                // 查找符号表中是否已定义此字面量
                if (symbol_table.find(literal_name) != symbol_table.end()) {
                    // 使用符号表中定义的属性，但保持类型为LITERAL
                    Symbol& existing_sym = symbol_table[literal_name];
                    symbol.precedence = existing_sym.precedence;
                    symbol.assoc = existing_sym.assoc;
                    symbol.value_type = existing_sym.value_type;
                }

                // 将字面量添加到临时符号表
                temp_symbols[symbol.name] = symbol;
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

        // 判断符号类型并从符号表中复制属性
        if (symbol_table.find(identifier) != symbol_table.end()) {
            // 使用符号表中的完整信息
            symbol = symbol_table[identifier];
        } else {
            // 未在声明部分定义，假设是非终结符
            symbol.name = identifier;
            symbol.type = ElementType::NON_TERMINAL;

            // 记录到临时符号表，以便后续验证
            temp_symbols[identifier] = symbol;
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

void YaccParser::validateSymbols()
{
    // 统计未定义的符号和以终结符作为左部的错误
    int undefined_symbol_count = 0;
    std::vector<std::string> undefined_symbols;

    // 直接从temp_symbols中提取用作非终结符但未定义的符号
    for (const auto& [name, symbol] : temp_symbols) {
        // 如果是被当作非终结符使用，但不在已定义的非终结符集合中
        if (symbol.type == ElementType::NON_TERMINAL && defined_non_terminals.find(name) == defined_non_terminals.end()) {
            undefined_symbol_count++;
            undefined_symbols.push_back(name);
            std::cerr << "警告: 符号 \"" << name << "\" 被使用但未被定义为终结符且没有产生式规则"
                      << std::endl;
        }
    }

    // 如果有未定义的符号，显示总数
    if (undefined_symbol_count > 0) {
        std::cerr << "警告: " << undefined_symbol_count << " 项非终结语词在文法中无用" << std::endl;

        // 输出每个未定义的符号
        for (const auto& name : undefined_symbols) {
            std::cerr << "警告: 非终结语词在文法中无用：" << name << std::endl;
        }
    }

    // 现在更新symbol_table，加入所有在语法规则中定义或使用的符号
    for (const auto& [name, symbol] : temp_symbols) {
        // 如果符号不在symbol_table中，进行更新
        if (symbol_table.find(name) == symbol_table.end()) {
            symbol_table[name] = symbol;
        }
    }

    // 将所有产生式左部的非终结符加入symbol_table
    for (const auto& [name, symbol] : defined_non_terminals) {
        // 如果符号不在symbol_table中，进行更新
        if (symbol_table.find(name) == symbol_table.end()) {
            symbol_table[name] = symbol;
        } else if (!symbol.value_type.empty() && symbol_table[name].value_type.empty()) {
            // 更新类型信息
            symbol_table[name].value_type = symbol.value_type;
        }
    }
}

void YaccParser::printParsedInfo() const
{
    std::cout << "起始符号: " << start_symbol << std::endl;

    // 计算终结符和非终结符的数量
    int token_count = 0;
    int non_terminal_count = 0;
    int literal_count = 0;

    for (const auto& [name, symbol] : symbol_table) {
        switch (symbol.type) {
        case ElementType::TOKEN:
            token_count++;
            break;
        case ElementType::NON_TERMINAL:
            non_terminal_count++;
            break;
        case ElementType::LITERAL:
            literal_count++;
            break;
        }
    }

    // 输出符号统计信息
    std::cout << "\n符号统计:" << std::endl;
    std::cout << "  终结符: " << token_count << std::endl;
    std::cout << "  非终结符: " << non_terminal_count << std::endl;
    std::cout << "  字面量: " << literal_count << std::endl;

    // 输出union定义
    if (!union_code.empty()) {
        std::cout << "\n\nUnion 定义:\n"
                  << union_code << std::endl;
    }

    std::cout << "\n\n产生式规则 (" << productions.size() << "):" << std::endl;
    for (const auto& prod : productions) {
        std::cout << prod.left.name;
        bool has_type = !prod.left.value_type.empty();
        bool has_prec = prod.precedence > 0;
        if (has_type || has_prec) {
            std::cout << " [";
            if (has_type)
                std::cout << prod.left.value_type;
            if (has_type && has_prec)
                std::cout << ", ";
            if (has_prec)
                std::cout << "优先级:" << prod.precedence;
            std::cout << "]";
        }
        std::cout << " -> ";
        for (const auto& sym : prod.right) {
            std::cout << sym.name << " ";
        }
        if (!prod.semantic_action.empty()) {
            std::cout << prod.semantic_action;
        }
        std::cout << std::endl;
    }
}

} // namespace seuyacc