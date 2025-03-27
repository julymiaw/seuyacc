#include "seuyacc/parser.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace seuyacc {
bool YaccParser::parseYaccFile(const std::string& filename)
{
    // 第一阶段：收集非终结符
    collectNonTerminals(filename);

    // 第二阶段：完整解析
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "无法打开文件: " << filename << std::endl;
        return false;
    }

    std::string line;
    bool in_definitions = true;
    bool in_rules = false;
    bool in_code = false;

    current_rule_name = "";
    parser_state = ParserState::EXPECTING_RULE_NAME;

    while (std::getline(file, line)) {
        // 去除注释和尾部空白
        line = std::regex_replace(line, std::regex("//.*$"), "");
        line = std::regex_replace(line, std::regex("^\\s+|\\s+$"), "");

        if (line.empty())
            continue;

        // 识别各个section
        if (line == "%%") {
            if (in_definitions) {
                in_definitions = false;
                in_rules = true;
            } else if (in_rules) {
                in_rules = false;
                in_code = true;
            }
            continue;
        }

        if (in_definitions) {
            // 处理定义部分
            if (line.substr(0, 6) == "%token") {
                parseTokenSection(line);
            } else if (line.substr(0, 6) == "%start") {
                parseStartSymbol(line);
            }
        } else if (in_rules) {
            // 处理语法规则部分
            parseGrammarRules(line);
        }
        // 忽略code部分
    }

    return true;
}

void YaccParser::collectNonTerminals(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
        return;

    std::string line;
    bool in_rules = false;
    bool expecting_colon = false;
    std::string current_rule = "";

    while (std::getline(file, line)) {
        // 预处理：去除注释和空白
        line = std::regex_replace(line, std::regex("//.*$"), "");
        line = std::regex_replace(line, std::regex("^\\s+|\\s+$"), "");

        if (line.empty())
            continue;

        // 检测规则部分
        if (line == "%%") {
            if (!in_rules)
                in_rules = true;
            else
                break; // 第二个%%，规则部分结束
            continue;
        }

        if (!in_rules)
            continue;

        // 检查是否有分号，表示一条规则结束
        bool has_semicolon = (line.find(';') != std::string::npos);

        // 状态机逻辑
        if (!expecting_colon) {
            // 可能是规则名开始
            std::smatch matches;
            if (std::regex_match(line, matches, std::regex("^(\\w+)$"))) {
                // 独立一行的规则名
                current_rule = line;
                expecting_colon = true;
            } else if (std::regex_match(line, matches, std::regex("^(\\w+)\\s*:"))) {
                // 同一行的"name:"形式
                current_rule = matches[1].str();
                non_terminals.insert(current_rule);
                expecting_colon = false; // 已找到冒号
            }
        } else {
            // 期待冒号
            if (line[0] == ':' || line.find(':') != std::string::npos) {
                // 找到冒号，确认当前规则名为非终结符
                non_terminals.insert(current_rule);
                expecting_colon = false;
            }
        }

        // 如果规则结束，重置状态
        if (has_semicolon) {
            expecting_colon = false;
        }
    }

    file.close();
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

void YaccParser::parseGrammarRules(const std::string& line)
{
    // 检查规则结束标志
    bool has_semicolon = (line.find(';') != std::string::npos);

    // 状态机解析
    switch (parser_state) {
    case ParserState::EXPECTING_RULE_NAME:
        // 规则名可能是单独的标识符
        if (std::regex_match(line, std::regex("^(\\w+)$"))) {
            current_rule_name = line;
            parser_state = ParserState::EXPECTING_COLON;
        }
        // 规则名可能直接接冒号
        else if (std::regex_match(line, std::regex("^(\\w+)\\s*:"))) {
            std::string rule_part = line.substr(0, line.find(':'));
            current_rule_name = std::regex_replace(rule_part, std::regex("\\s+"), "");

            // 提取冒号后面的产生式
            std::string production = line.substr(line.find(':') + 1);
            processProductionPart(production);

            parser_state = ParserState::IN_PRODUCTION;
            if (has_semicolon) {
                parser_state = ParserState::EXPECTING_RULE_NAME;
            }
        }
        break;

    case ParserState::EXPECTING_COLON:
        // 以冒号开头的行，表示产生式开始
        if (line[0] == ':') {
            std::string production = line.substr(1); // 跳过冒号
            processProductionPart(production);
            parser_state = ParserState::IN_PRODUCTION;

            if (has_semicolon) {
                parser_state = ParserState::EXPECTING_RULE_NAME;
            }
        }
        // 冒号可能在行中间而不是开头
        else if (line.find(':') != std::string::npos) {
            // 提取冒号后面的产生式
            std::string production = line.substr(line.find(':') + 1);
            processProductionPart(production);

            parser_state = ParserState::IN_PRODUCTION;
            if (has_semicolon) {
                parser_state = ParserState::EXPECTING_RULE_NAME;
            }
        }
        break;

    case ParserState::IN_PRODUCTION:
        // 处理继续的产生式（以|开头）
        if (line[0] == '|') {
            std::string production = line.substr(1); // 跳过|
            processProductionPart(production);
        }
        // 或者处理任何不以|开头但是属于当前规则的行
        else {
            processProductionPart(line);
        }

        if (has_semicolon) {
            parser_state = ParserState::EXPECTING_RULE_NAME;
        }
        break;

    case ParserState::END_OF_RULE:
        // 规则结束，期待新规则
        parser_state = ParserState::EXPECTING_RULE_NAME;
        // 递归调用以处理可能的新规则
        parseGrammarRules(line);
        break;
    }
}

void YaccParser::processProductionPart(const std::string& part)
{
    std::string cleaned = part;

    // 移除开头和结尾的分隔符
    cleaned = std::regex_replace(cleaned, std::regex("^\\s*\\|"), "");

    // 处理分号 - 实现智能检测，跳过引号中的分号
    bool in_single_quote = false;
    bool in_double_quote = false;
    size_t semicolon_pos = std::string::npos;

    for (size_t i = 0; i < cleaned.length(); i++) {
        char c = cleaned[i];

        // 处理引号开关状态
        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
        } else if (c == '\"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        }
        // 只检测不在引号内的分号
        else if (c == ';' && !in_single_quote && !in_double_quote) {
            semicolon_pos = i;
            break;
        }
    }

    // 如果找到非引号内的分号，则截取分号前的部分
    if (semicolon_pos != std::string::npos) {
        cleaned = cleaned.substr(0, semicolon_pos);
    }

    cleaned = std::regex_replace(cleaned, std::regex("^\\s+|\\s+$"), "");

    // 如果为空，不处理
    if (cleaned.empty()) {
        return;
    }

    // 创建新产生式
    Production prod;

    // 设置左部为Symbol对象
    Symbol left_sym;
    left_sym.name = current_rule_name;
    left_sym.type = ElementType::NON_TERMINAL; // 左部总是非终结符
    prod.left = left_sym;

    parseProductionRightSide(cleaned, prod);
    productions.push_back(prod);
}

void YaccParser::parseProductionRightSide(const std::string& right_side,
    Production& prod)
{
    std::vector<Symbol> symbols;
    std::string current_token;
    bool in_single_quote = false;
    bool in_double_quote = false;

    for (size_t i = 0; i < right_side.length(); i++) {
        char c = right_side[i];

        // 处理引号
        if (c == '\'' && !in_double_quote) {
            if (!in_single_quote) {
                // 开始单引号
                if (!current_token.empty()) {
                    Symbol sym;
                    sym.name = current_token;
                    sym.type = determineElementType(current_token);
                    symbols.push_back(sym);
                    current_token.clear();
                }
                current_token += c;
                in_single_quote = true;
            } else {
                // 结束单引号
                current_token += c;
                Symbol sym;
                sym.name = current_token;
                sym.type = determineElementType(current_token);
                symbols.push_back(sym);
                current_token.clear();
                in_single_quote = false;
            }
            continue;
        }

        // 处理双引号
        if (c == '\"' && !in_single_quote) {
            if (!in_double_quote) {
                // 开始双引号
                if (!current_token.empty()) {
                    Symbol sym;
                    sym.name = current_token;
                    sym.type = determineElementType(current_token);
                    symbols.push_back(sym);
                    current_token.clear();
                }
                current_token += c;
                in_double_quote = true;
            } else {
                // 结束双引号
                current_token += c;
                Symbol sym;
                sym.name = current_token;
                sym.type = determineElementType(current_token);
                symbols.push_back(sym);
                current_token.clear();
                in_double_quote = false;
            }
            continue;
        }

        // 处理空白字符
        if (!in_single_quote && !in_double_quote && std::isspace(c)) {
            if (!current_token.empty()) {
                Symbol sym;
                sym.name = current_token;
                sym.type = determineElementType(current_token);
                symbols.push_back(sym);
                current_token.clear();
            }
            continue;
        }

        // 添加到当前token
        current_token += c;
    }

    // 处理最后一个token
    if (!current_token.empty()) {
        Symbol sym;
        sym.name = current_token;
        sym.type = determineElementType(current_token);
        symbols.push_back(sym);
    }

    prod.right = symbols;
}

ElementType YaccParser::determineElementType(const std::string& symbol)
{
    // 1. 检查是否是已知的非终结符
    if (non_terminals.find(symbol) != non_terminals.end()) {
        return ElementType::NON_TERMINAL;
    }

    // 2. 检查是否是已声明的TOKEN
    if (token_map.find(symbol) != token_map.end()) {
        return ElementType::TOKEN;
    }

    // 3. 引号中的内容是字面量
    if ((symbol.length() >= 2 && symbol.front() == '\'' && symbol.back() == '\'') || (symbol.length() >= 2 && symbol.front() == '"' && symbol.back() == '"')) {
        return ElementType::LITERAL;
    }

    // 4. 全部大写字母、数字和下划线的通常可能是未声明的TOKEN
    if (!symbol.empty() && std::all_of(symbol.begin(), symbol.end(), [](char c) {
            return std::isupper(c) || c == '_' || std::isdigit(c);
        })) {
        // 警告但仍然作为TOKEN处理
        std::cerr << "警告: 发现未声明的可能TOKEN: " << symbol << std::endl;
        return ElementType::TOKEN;
    }

    // 5. 无法确定符号类型，抛出异常
    throw std::runtime_error("无法确定符号类型: " + symbol);
}

void YaccParser::printParsedInfo() const
{
    std::cout << "起始符号: " << start_symbol << std::endl;

    std::cout << "\nTokens (" << tokens.size() << "):" << std::endl;
    for (const auto& token : tokens) {
        std::cout << token.name << " ";
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
        std::cout << std::endl;
    }
}

} // namespace seuyacc