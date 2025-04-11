#include "seuyacc/lr_generator.h"
#include "seuyacc/parser.h"
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    bool generate_plantUML = false;
    bool generate_markdown = false;
    std::string input_file;

    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--plantUML" || arg == "-p") {
            generate_plantUML = true;
        } else if (arg == "--markdown" || arg == "-m") {
            generate_markdown = true;
        } else if (input_file.empty()) {
            input_file = arg;
        }
    }

    if (input_file.empty()) {
        std::cerr << "用法: " << argv[0] << " [--plantUML|-p] <yacc文件路径>\n";
        return 1;
    }

    seuyacc::YaccParser parser;
    if (parser.parseYaccFile(input_file)) {
        parser.printParsedInfo();

        if (parser.productions.empty()) {
            std::cerr << "警告: 没有解析到任何产生式规则!\n";
            return 1;
        }

        // 生成LR(1)分析表
        std::cout << "\n正在生成LR(1)分析表...\n";

        try {
            seuyacc::LRGenerator generator(parser);
            generator.generateTable();
            std::cout << "分析表生成完成\n";

            // 提取基本文件名（不含扩展名）
            std::string base_filename = input_file;

            size_t last_dot = base_filename.find_last_of('.');
            if (last_dot != std::string::npos) {
                base_filename = base_filename.substr(0, last_dot);
            }

            // 如果需要生成PlantUML输出
            if (generate_plantUML) {
                std::string plantUML = generator.toPlantUML();

                // 使用基本文件名创建输出文件
                std::string output_file = base_filename + ".puml";

                std::ofstream out_file(output_file);
                if (out_file.is_open()) {
                    out_file << plantUML;
                    out_file.close();
                    std::cout << "PlantUML状态图已生成: " << output_file << std::endl;
                } else {
                    std::cerr << "无法创建PlantUML输出文件\n";
                }
            }

            if (generate_markdown) {
                std::string markdown_table = generator.toMarkdownTable();

                // 使用基本文件名创建输出文件
                std::string output_file = base_filename + ".md";

                std::ofstream out_file(output_file);
                if (out_file.is_open()) {
                    out_file << markdown_table;
                    out_file.close();
                    std::cout << "Markdown格式的LR(1)分析表已生成: " << output_file << std::endl;
                } else {
                    std::cerr << "无法创建Markdown输出文件\n";
                }
            }

            std::cout << "LR(1)分析表生成完成\n";
        }

        catch (const std::exception& e) {
            std::cerr << "生成LR(1)分析表时发生异常: " << e.what() << std::endl;
            return 1;
        } catch (...) {
            std::cerr << "生成LR(1)分析表时发生未知异常!" << std::endl;
            return 1;
        }
    } else {
        std::cerr << "解析失败!\n";
        return 1;
    }

    return 0;
}