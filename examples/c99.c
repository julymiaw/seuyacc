/*
 * 一个简单的 C99 测试程序
 * 用于测试 SeuYacc 生成的解析器
 */

// 全局变量声明
int global_var = 100;
const float PI = 3.14159f;
extern char external_char; // 外部变量

// 结构体定义
struct Point {
    int x;
    double y;
};

// 函数声明 (原型)
int add(int a, int b);
void print_point(struct Point p);

// inline 函数 (C99 特性)
inline int max(int a, int b)
{
    return a > b ? a : b;
}

// 主函数
int main(void)
{
    // 局部变量声明与初始化
    int i = 0;
    int limit = 10;
    float result = 0.0;
    struct Point pt = { 1, 2.5 }; // 结构体初始化
    int numbers[5] = { 1, 2, 3, 4, 5 }; // 数组初始化
    char message[] = "Hello, C99!"; // 字符串字面量

    // for 循环
    for (i = 0; i < limit; i++) {
        if (i % 2 == 0) { // if 语句 和 取模运算
            result += add(i, global_var); // 函数调用 和 加法赋值
        } else {
            result -= 5.0f; // 减法赋值
        }
        // 单行注释测试
    }

    // 指针操作
    int* ptr = &i;
    *ptr = max(i, limit); // 调用 inline 函数

    // 结构体访问
    print_point(pt);
    pt.x = *ptr;

    // switch 语句
    switch (i / 5) {
    case 0:
        // printf("Low\n"); // printf 调用语法
        break;
    case 1:
        // printf("Medium\n");
        break;
    default:
        // printf("High\n");
        break;
    }

    // while 循环
    while (i > 0) {
        i--; // 后缀自减
        if (i < 3)
            continue; // continue 语句
        if (i > 8)
            break; // break 语句
    }

    // goto 语句 (尽量少用)
    if (result > 1000.0) {
        goto end_label;
    }

    // 表达式语句
    result * 2.0; // 有效但无效果的表达式

end_label:
    // printf("Final result: %f\n", result);
    // printf("Message: %s\n", message);

    return 0; // return 语句
}

// 函数定义
int add(int a, int b)
{
    return a + b;
}

void print_point(struct Point p)
{
    // printf("Point: (%d, %f)\n", p.x, p.y);
}

// 另一个函数定义，可能在其他文件
// char external_char = 'A';