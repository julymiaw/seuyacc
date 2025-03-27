#include "seuyacc/parser.h"
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "用法: " << argv[0] << " <yacc文件路径>\n";
        return 1;
    }

    seuyacc::YaccParser parser;
    if (parser.parseYaccFile(argv[1])) {
        parser.printParsedInfo();
        std::cout << "\n解析了 " << parser.productions.size() << " 条产生式规则\n";
        if (parser.productions.empty()) {
            std::cerr << "警告: 没有解析到任何产生式规则!\n";
        }
    } else {
        std::cerr << "解析失败!\n";
        return 1;
    }

    return 0;
}