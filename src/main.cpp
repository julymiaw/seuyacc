#include "seuyacc/lr_generator.h"
#include "seuyacc/parser.h"
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    bool generate_plantUML = false;
    bool generate_markdown = false;
    bool generate_header = false; // 新增头文件生成选项
    std::string input_file;

    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--plantUML" || arg == "-p") {
            generate_plantUML = true;
        } else if (arg == "--markdown" || arg == "-m") {
            generate_markdown = true;
        } else if (arg == "--definitions" || arg == "-d") {
            generate_header = true; // 兼容 Yacc/Bison 的 -d 选项
        } else if (input_file.empty()) {
            input_file = arg;
        }
    }

    if (input_file.empty()) {
        std::cerr << "用法: " << argv[0] << " [选项...] <yacc文件路径>\n";
        std::cerr << "选项:\n";
        std::cerr << "  -p, --plantUML      生成状态机的 PlantUML 图\n";
        std::cerr << "  -m, --markdown      生成 Markdown 格式的分析表\n";
        std::cerr << "  -d, --definitions   生成包含令牌定义的头文件 (y.tab.h)\n";
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

            // 提取输入文件的目录和文件名（不含扩展名）
            std::string file_dir;
            std::string file_name_without_ext;

            // 1. 分离路径和文件名
            size_t last_slash = input_file.find_last_of("/\\");
            if (last_slash != std::string::npos) {
                // 有路径部分
                file_dir = input_file.substr(0, last_slash + 1);
                file_name_without_ext = input_file.substr(last_slash + 1);
            } else {
                // 没有路径部分，文件在当前目录
                file_dir = "";
                file_name_without_ext = input_file;
            }

            // 2. 去除文件扩展名
            size_t last_dot = file_name_without_ext.find_last_of('.');
            if (last_dot != std::string::npos) {
                file_name_without_ext = file_name_without_ext.substr(0, last_dot);
            }

            // 如果需要生成PlantUML输出
            if (generate_plantUML) {
                std::string plantUML = generator.toPlantUML();

                // 使用正确的文件名创建输出文件
                std::string output_file = file_dir + file_name_without_ext + ".puml";

                std::ofstream out_file(output_file);
                if (out_file.is_open()) {
                    out_file << plantUML;
                    out_file.close();
                    std::cout << "PlantUML状态图已生成: " << output_file << std::endl;
                } else {
                    std::cerr << "无法创建PlantUML输出文件\n";
                }
            }

            // 如果需要生成Markdown表格
            if (generate_markdown) {
                std::string markdown_table = generator.toMarkdownTable();

                // 使用正确的文件名创建输出文件
                std::string output_file = file_dir + file_name_without_ext + ".md";

                std::ofstream out_file(output_file);
                if (out_file.is_open()) {
                    out_file << markdown_table;
                    out_file.close();
                    std::cout << "Markdown格式的LR(1)分析表已生成: " << output_file << std::endl;
                } else {
                    std::cerr << "无法创建Markdown输出文件\n";
                }
            }

            // 如果需要生成头文件
            if (generate_header) {
                // 使用基本文件名作为头文件名
                std::string header_name = file_name_without_ext + ".tab.h";
                std::string output_file = file_dir + header_name;

                std::string header_content = generator.generateHeaderFile(header_name);

                std::ofstream out_file(output_file);
                if (out_file.is_open()) {
                    out_file << header_content;
                    out_file.close();
                    std::cout << "令牌定义头文件已生成: " << output_file << std::endl;
                } else {
                    std::cerr << "无法创建头文件: " << output_file << std::endl;
                }
            }

            std::cout << "LR(1)分析表生成完成\n";
        } catch (const std::exception& e) {
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