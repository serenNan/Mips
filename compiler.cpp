#include <iostream>
#include <cstdio>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <map>
#include <stack>


// --- 宏定义和全局常量 ---
const char* keywords[] = {
        "const","int","static","break","continue",
        "if","main","else","for","return","void","printf"
};
const char* keymap[] = {
        "CONSTTK","INTTK","STATICTK","BREAKTK",
        "CONTINUETK","IFTK","MAINTK","ELSETK","FORTK","RETURNTK","VOIDTK","PRINTFTK"
};
#define KEYWORD_COUNT (sizeof(keywords) / sizeof(keywords[0]))
#define MAX_TOKEN_LEN 10000


// --- 全局数据和结构体 ---
struct Token {
    std::string type;
    std::string value;
    int line;
};

struct FileErrorRecord {
    int line;
    char type;
};
static int g_scope_counter=0;
static int g_insert_id = 0;
// 增强的 Symbol 结构体
struct Symbol {
    std::string name;
    std::string type;          // "int", "void", "function"
    bool is_const;             // 是否为常量
    bool is_static;            // 是否为静态局部变量
    std::vector<int> dimensions; // 数组维数 (0表示非数组, >0 表示数组)
    int line_declared;
    int scope_id;
    int param_count = -1;
    std::vector<std::string> param_types;
    std::string llvm_name;     // 存储变量的 LLVM IR 地址 (e.g., "@test" 或 "%test_addr")
    std::string llvm_type;     // 存储 LLVM IR 类型 (e.g., "i32", "i32*")
    bool is_global = false;    // 是否为全局变量
    bool is_param = false;     // 是否为函数参数
    std::vector<int> const_init_values;
};
struct SymbolOutputRecord {
    int scope_id;
    std::string name;
    std::string type_name; // 任务要求的类型名称
    int line_declared; // 用于排序
    int insert_id;
};

// ------------------- 新增 LLVM IR 辅助结构 -------------------
struct IRValue {
    std::string name; // LLVM IR 中的寄存器名或常量值 (e.g., "%1", "5", "@test_addr")
    std::string type; // LLVM IR 中的类型 (e.g., "i32", "i32*", "i8*")
    // 构造函数简化
    IRValue(const std::string& n = "", const std::string& t = "i32") : name(n), type(t) {}
};
struct FuncRParamsResult {
    std::vector<IRValue> ir_values;        // 用于 LLVM IR 生成
    std::vector<std::string> semantic_types; // 用于 D/E 错误检查
};
std::vector<SymbolOutputRecord> g_symbol_output_records;
FILE* g_symbol_file = nullptr;
std::vector<Token> g_tokens;
using SymbolTable = std::map<std::string, Symbol>;
std::vector<SymbolTable> g_scope_stack; // 作用域栈
std::stack<int> g_active_scope_ids;

FILE* g_error_file = nullptr;
#define ERROR_b(line) do { if (g_error_file) fprintf(g_error_file, "%d b\n", line); } while(0)
#define ERROR_c(line) do { if (g_error_file) fprintf(g_error_file, "%d c\n", line); } while(0)
#define ERROR_d(line) do { if (g_error_file) fprintf(g_error_file, "%d d\n", line); } while(0)
#define ERROR_e(line) do { if (g_error_file) fprintf(g_error_file, "%d e\n", line); } while(0)
#define ERROR_f(line) do { if (g_error_file) fprintf(g_error_file, "%d f\n", line); } while(0)
#define ERROR_g(line) do { if (g_error_file) fprintf(g_error_file, "%d g\n", line); } while(0)
#define ERROR_h(line) do { if (g_error_file) fprintf(g_error_file, "%d h\n", line); } while(0)
#define ERROR_i(line) do { if (g_error_file) fprintf(g_error_file, "%d i\n", line); } while(0)
#define ERROR_j(line) do { if (g_error_file) fprintf(g_error_file, "%d j\n", line); } while(0)
#define ERROR_k(line) do { if (g_error_file) fprintf(g_error_file, "%d k\n", line); } while(0)
#define ERROR_l(line) do { if (g_error_file) fprintf(g_error_file, "%d l\n", line); } while(0)
#define ERROR_m(line) do { if (g_error_file) fprintf(g_error_file, "%d m\n", line); } while(0)
void sort_error_file(const char* error_path) {
    std::vector<FileErrorRecord> records;
    std::ifstream infile(error_path);

    if (!infile.is_open()) {
        // 如果文件不存在或无法打开（例如，如果根本没有错误输出），则跳过
        return;
    }

    std::string line;
    while (std::getline(infile, line)) {
        if (line.empty()) continue;

        std::stringstream ss(line);
        int line_num;
        char error_type;

        // 尝试解析：格式应为 "行号 错误类型"
        if (ss >> line_num >> error_type) {
            records.push_back({line_num, error_type});
        }
    }
    infile.close();

    // 排序：按行号从小到大排序
    std::sort(records.begin(), records.end(),
              [](const FileErrorRecord& a, const FileErrorRecord& b) {
                  return a.line < b.line;
              });

    // 将排序后的结果写回文件 (使用 std::ofstream 自动覆盖)
    std::ofstream outfile(error_path);
    if (!outfile.is_open()) {
        fprintf(stderr, "Error: Failed to open error file for sorting rewrite: %s\n", error_path);
        return;
    }

    for (const auto& record : records) {
        outfile << record.line << " " << record.type << "\n";
    }

    outfile.close();
}

void pretreatment(const char* getfilepath, const char* putfilepath) {
    FILE* yuan = nullptr;
    FILE* yuchli = nullptr;
    int ch; // 始终使用 int 接收 fgetc 的返回值

    // 使用标准的 fopen
    yuan = fopen(getfilepath, "r");
    if (yuan == nullptr) {
        printf("Source file failed to open! Path: %s\n", getfilepath);
        exit(1);
    }
    yuchli = fopen(putfilepath,"w");
    if(yuchli == nullptr){
        printf("Preprocessing file failed to open! Path: %s\n", putfilepath);
        fclose(yuan);
        exit(1);
    }

    while ((ch = fgetc(yuan)) != EOF) {
        // 注释处理
        if (ch == '/') {
            int ch2 = fgetc(yuan);
            if (ch2 == '/') { // 单行注释 //
                while ((ch = fgetc(yuan)) != EOF && ch != '\n');
                if (ch == '\n') {
                    fputc(ch, yuchli);
                }
                continue;
            } else if (ch2 == '*') { // 多行注释 /* */
                int prev_ch = 0;
                while ((ch = fgetc(yuan)) != EOF) {
                    if (ch == '/' && prev_ch == '*') break;
                    prev_ch = ch;
                    if (ch == '\n') {
                        fputc(ch, yuchli);
                    }
                }
                continue;
            } else {
                fputc(ch, yuchli);
                ch = ch2; // 将下一个字符 ch2 继续处理
            }
        }

        // 跳过或处理多字节字符
        if (ch < 0 && ch != EOF) {
            continue;
        }

        fputc(ch, yuchli);
    }
    fclose(yuan);
    fclose(yuchli);
}

// --- 词法分析辅助函数 ---

void push_token(const char* type, const char* value, int row) {
    g_tokens.push_back({std::string(type), std::string(value), row});
}

// [省略 pretreatment 函数，假设其能正确处理注释]

void lexical_analysis(const char* getfilepath, const char* putlexerpath, const char* puterrorpath) {
    FILE* yuchli = fopen(getfilepath, "r");
    if (yuchli == nullptr) { exit(1); }

    // 打开错误文件
    g_error_file = fopen(puterrorpath, "w");
    if (g_error_file == nullptr) { fclose(yuchli); exit(1); }

    g_tokens.clear();
    int ch;
    char token_buf[MAX_TOKEN_LEN] = {};
    int row = 1;

    while ((ch = fgetc(yuchli)) != EOF) {
        if (std::isspace(ch)) { if (ch == '\n') row++; continue; }

        if (ch > 0 && (std::isalpha(ch) || ch == '_')) {
            // 标识符/关键字 (不变)
            int tokenindex = 0;
            const char* token_type = "IDENFR";
            // ... (省略实现细节，假设正确)
            token_buf[tokenindex++] = static_cast<char>(ch);
            while ((ch = fgetc(yuchli)) != EOF && ch > 0 && (std::isalnum(ch) || ch == '_')) {
                if (tokenindex < MAX_TOKEN_LEN - 1) token_buf[tokenindex++] = static_cast<char>(ch);
                else { ungetc(ch, yuchli); break; }
            }
            if (ch != EOF) ungetc(ch, yuchli);
            token_buf[tokenindex] = '\0';
            for (int i = 0; i < KEYWORD_COUNT; i++) {
                if (std::strcmp(token_buf, keywords[i]) == 0) {
                    token_type = keymap[i];
                    break;
                }
            }
            push_token(token_type, token_buf, row);
            continue;
        }

        else if (ch > 0 && std::isdigit(ch)) {
            // 常数 (不变)
            int tokenindex = 0;
            token_buf[tokenindex++] = static_cast<char>(ch);
            while ((ch = fgetc(yuchli)) != EOF && ch > 0 && std::isdigit(ch)) {
                if (tokenindex < MAX_TOKEN_LEN - 1) token_buf[tokenindex++] = static_cast<char>(ch);
                else { ungetc(ch, yuchli); break; }
            }
            if (ch != EOF) ungetc(ch, yuchli);
            token_buf[tokenindex] = '\0';
            push_token("INTCON", token_buf, row);
            continue;
        }

        else if (ch == '"') {
            // 字符串 (InitVal 中允许)
            int tokenindex = 0;
            token_buf[tokenindex++] = static_cast<char>(ch);
            while ((ch = fgetc(yuchli)) != EOF && ch != '\n') {
                if (ch == '"') {
                    token_buf[tokenindex++] = static_cast<char>(ch);
                    token_buf[tokenindex] = '\0';
                    push_token("STRCON", token_buf, row);
                    break;
                }
                token_buf[tokenindex++] = static_cast<char>(ch);
            }
            if (ch == '\n' || ch == EOF) {
                fprintf(g_error_file, "%d a\n", row); // 非法符号 a
            }
            continue;
        }

        else if (ch > 0 && std::isgraph(ch)) {
            // 运算符与界符 (重点处理 '&' 和 '|')
            const char* token_type = nullptr;
            int next_ch;
            token_buf[0] = '\0';

            switch (ch) {
                case '&':
                    next_ch = fgetc(yuchli);
                    if (next_ch == '&') { token_type = "AND"; std::strcpy(token_buf, "&&"); }
                    else { ungetc(next_ch, yuchli);
                        fprintf(g_error_file, "%d a\n", row); // 输出错误 a

                        // **强制作为 '&&' 处理并继续**
                        token_type = "AND";
                        std::strcpy(token_buf, "&&"); }
                    break;
                case '|':
                    next_ch = fgetc(yuchli);
                    if (next_ch == '|') { token_type = "OR"; std::strcpy(token_buf, "||"); }
                    else { ungetc(next_ch, yuchli);
                        fprintf(g_error_file, "%d a\n", row); // 输出错误 a

                        // **强制作为 '&&' 处理并继续**
                        token_type = "OR";
                        std::strcpy(token_buf, "||"); }
                    break;
                case '+':
                    token_type = "PLUS"; token_buf[0] = '+'; token_buf[1] = '\0'; break;
                case '-':
                    token_type = "MINU"; token_buf[0] = '-'; token_buf[1] = '\0'; break;
                case '*':
                    token_type = "MULT"; token_buf[0] = '*'; token_buf[1] = '\0'; break;
                case '/':
                    token_type = "DIV"; token_buf[0] = '/'; token_buf[1] = '\0'; break;
                case '%':
                    token_type = "MOD"; token_buf[0] = '%'; token_buf[1] = '\0'; break;
                case '!':
                case '=':
                case '<':
                case '>':
                    next_ch = fgetc(yuchli);
                    token_buf[0] = static_cast<char>(ch);
                    if (next_ch == '=') {
                        token_buf[1] = '='; token_buf[2] = '\0';
                        if (ch == '!') token_type = "NEQ";
                        else if (ch == '=') token_type = "EQL";
                        else if (ch == '<') token_type = "LEQ";
                        else if (ch == '>') token_type = "GEQ";
                    } else {
                        ungetc(next_ch, yuchli);
                        token_buf[1] = '\0';
                        if (ch == '!') token_type = "NOT";
                        else if (ch == '=') token_type = "ASSIGN";
                        else if (ch == '<') token_type = "LSS";
                        else if (ch == '>') token_type = "GRE";
                    }
                    break;
                case '(': token_type = "LPARENT"; token_buf[0] = static_cast<char>(ch); token_buf[1] = '\0'; break;
                case ')': token_type = "RPARENT"; token_buf[0] = static_cast<char>(ch); token_buf[1] = '\0'; break;
                case '[': token_type = "LBRACK"; token_buf[0] = static_cast<char>(ch); token_buf[1] = '\0'; break;
                case ']': token_type = "RBRACK"; token_buf[0] = static_cast<char>(ch); token_buf[1] = '\0'; break;
                case '{': token_type = "LBRACE"; token_buf[0] = static_cast<char>(ch); token_buf[1] = '\0'; break;
                case '}': token_type = "RBRACE"; token_buf[0] = static_cast<char>(ch); token_buf[1] = '\0'; break;
                case ';': token_type = "SEMICN"; token_buf[0] = static_cast<char>(ch); token_buf[1] = '\0'; break;
                case ',': token_type = "COMMA"; token_buf[0] = static_cast<char>(ch); token_buf[1] = '\0'; break;
                default:
                    fprintf(g_error_file, "%d a\n", row); // 非法符号 a
                    continue;
            }

            if (token_type != nullptr) {
                push_token(token_type, token_buf, row);
            }
            continue;
        }

        else {
            fprintf(g_error_file, "%d a\n", row); // 非法符号 a
            continue;
        }
    }

    FILE* cifa = fopen(putlexerpath, "w");
    if(cifa != nullptr){
        for(const auto& tok : g_tokens) { fprintf(cifa, "%s %s\n", tok.type.c_str(), tok.value.c_str()); }
        fclose(cifa);
    }

    fclose(yuchli);
    //if(g_error_file) fclose(g_error_file);
}

class IRGenerator {
private:
    std::stringstream global_ir;     // 用于全局声明、变量和字符串
    std::stringstream function_ir;   // 用于当前正在生成的函数体
    std::string all_function_definitions_;
    int register_count = 1;          // 临时寄存器计数器 (%1, %2, ...)
    int label_count = 0;             // 基本块标签计数器 (label0, label1, ...)
    int string_id_count = 0;         // 字符串常量计数器 (@.str0, @.str1, ...)

    // 用于在 Block 块的 entry 处统一放置 alloca
    std::stringstream alloca_ir_buffer;

    // 当前活跃的基本块，用于分支控制
    std::string current_block = "entry";

public:
    // ... 构造函数 ...

    std::string new_reg() { return "%" + std::to_string(register_count++); }
    std::string new_label(const std::string& prefix = "label") {
        return prefix + std::to_string(label_count++);
    }

    // 用于处理 printf 的字符串常量定义
    IRValue define_string(const std::string& str_val) {
        std::string str_name = "@.str" + std::to_string(string_id_count++);
        // 去掉两端引号
        std::string raw = str_val;
        if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
            raw = raw.substr(1, raw.size() - 2);
        }

        // 将原始字符串解析为字节序列：将 \n (反斜杠 + n) 识别为单个换行字节
        std::string bytes;
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '\\' && i + 1 < raw.size()) {
                char c = raw[i + 1];
                if (c == 'n') { bytes.push_back('\n'); ++i; continue; }
                if (c == '\\') { bytes.push_back('\\'); ++i; continue; }
                // 其它转义暂直接按两个字符处理（保守）
                bytes.push_back(raw[i]);
            } else {
                bytes.push_back(raw[i]);
            }
        }

        // 计算长度（包括终结符 NUL）
        size_t len = bytes.size() + 1;

        // 生成 llvm 可接受的显示形式：将换行字节转为 \0A，双引号和反斜杠做转义，其他直接放
        std::string llvm_content;
        for (unsigned char ch : bytes) {
            if (ch == '\n') llvm_content += "\\0A";
            else if (ch == '\\') llvm_content += "\\5C"; // backslash
            else if (ch == '"') llvm_content += "\\22";
            else if (ch < 32 || ch > 126) {
                // 非打印字符，使用 \HH 十六进制
                char buf[8];
                sprintf(buf, "\\%02X", (unsigned char)ch);
                llvm_content += buf;
            } else {
                llvm_content.push_back(ch);
            }
        }
        llvm_content += "\\00"; // NUL 终结符

        // 写入全局常量（global_ir）
        global_ir << str_name << " = private unnamed_addr constant [" << len
                  << " x i8] c\"" << llvm_content << "\", align 1\n";

        // 在函数体中生成 GEP（注意：调用此方法应在函数上下文中）
        std::string ptr_reg = new_reg();
        function_ir << ptr_reg << " = getelementptr inbounds [" << len << " x i8], ["
                    << len << " x i8]* " << str_name << ", i32 0, i32 0\n";

        return {ptr_reg, "i8*"};
    }

    void clear_function_body_ir() {
        function_ir.str("");
        function_ir.clear(); // 清除状态标志
    }

    // 获取 function_ir 缓冲区的内容，并清空它
    std::string get_and_clear_function_body_ir() {
        std::string content = function_ir.str();
        function_ir.str("");
        function_ir.clear();
        return content;
    }
    std::string get_function_body_ir() {
        std::string content = function_ir.str();
        function_ir.str("");
        function_ir.clear();
        return content;
    }
    void reset_register_count() {
        register_count = 0; // 确保从 %1 开始计数
    }
    void write_global(const std::string& ir) { global_ir << ir << "\n"; }
    void write_func(const std::string& ir) { function_ir << ir << "\n"; }
    void write_alloca(const std::string& ir) { alloca_ir_buffer << ir << "\n"; }

    // 在函数结束时，将 alloca 语句插入到 function_ir 的 entry 块开头
    std::string get_final_ir() {
        // ... (IO 声明) ...
        std::string final_ir =
                "declare i32 @getint()\n"
                "declare void @putint(i32)\n"
                "declare void @putch(i32)\n"
                "declare void @putstr(i8*)\n\n" +
                global_ir.str();

        // 将 alloca 缓冲区的内容插入到 entry 块
        // 注意：这需要在 parseMainFuncDef 或 parseFuncDef 中处理。
        // 为了简化，我们假设 function_ir 已经包含了 alloca 语句。

        final_ir += function_ir.str();
        return final_ir;
    }
};
// --- 语法分析器 (Parser) ---

class Parser {
private:
    size_t current_index;
    bool basic_block_terminated = false;
    FILE* output_file;
    SymbolTable g_builtin_symbols;
    std::stack<std::string> continue_label_stack;
    std::stack<std::string> break_label_stack;
    bool is_parsing_const_exp = false;
    //bool block_contains_return = false;
    std::string current_func_return_type = "void"; // 默认为 void
    int loop_depth_counter = 0;
    std::stack<std::string> loop_inc_labels; // continue语句的目标标签（循环增量块）
    std::stack<std::string> loop_end_labels;
    bool is_const_context = false;
    struct ConstValue {
        int value;
        bool is_valid = false;
    };
    std::stack<ConstValue> const_value_stack;
    // <--- 新增 LLVM IR 成员 --->
    IRGenerator ir_generator;
    std::stack<std::stringstream> alloca_buffers; // 用于收集局部变量 alloca
    // ------------------- 辅助函数 -------------------
    std::string infer_type_name(const Symbol& s) {
        bool is_array = !s.dimensions.empty();
        bool is_function = (s.param_count >= 0) ||
                           (s.name == "main" || s.name == "printf" || s.name == "getint" || s.name == "strlen" || s.name == "putch");
        // 1. 函数类型判断: 优先使用 param_count 或已知函数名来识别函数。
        // 确保 s.type 是 "int" 或 "void" (函数的返回类型)。
        if (is_function) {
            if (s.type == "int") {
                return "IntFunc"; // 解决 Int vs IntFunc 错误
            }
            if (s.type == "void") {
                return "VoidFunc"; // 解决 void vs VoidFunc 错误
            }
            // 如果 type 字段丢失，但我们确定它是函数，可以默认返回 IntFunc 以避免 UnknownType
            return "UnknownFuncType";
        }

        // 2. 变量类型判断 (基于 base type "int")
        // 如果 s.type 不是 "int"，那就是解析错误，这里默认只处理 "int"。
        if (s.type == "int") {
            if (s.is_const) {
                return is_array ? "ConstIntArray" : "ConstInt"; // 解决数组/常量数组错误
            }

            if (s.is_static) {
                return is_array ? "StaticIntArray" : "StaticInt";
            }

            return is_array ? "IntArray" : "Int";
        }

        return s.type; // 如果 type 是 "void"，但不是函数，不应该出现。如果出现，直接返回 "void" 或 "UnknownType"。
    }
    const Token& current_token() {
        if (current_index < g_tokens.size()) { return g_tokens[current_index]; }
        static const Token eof_token = {"EOF", "", -1}; return eof_token;
    }

    const Token& peek(int offset) {
        if (current_index + offset < g_tokens.size()) { return g_tokens[current_index + offset]; }
        static const Token eof_token = {"EOF", "", -1}; return eof_token;
    }

    void match(const std::string& expected_type) {
        const Token& tok = current_token();
        fprintf(output_file, "%s %s\n", tok.type.c_str(), tok.value.c_str());

        if (tok.type == expected_type) {
            current_index++;
        } else {
            fprintf(stderr, "Syntax Error at line %d: Expected %s, got %s\n",
                    tok.line, expected_type.c_str(), tok.type.c_str());
            exit(1);
        }
    }

    void match_with_error_check(const std::string& expected_type, char error_type, int error_line) {
        const Token& tok = current_token();

        if (tok.type == expected_type) {
            // 1. 匹配成功：输出 Token 并推进索引
            fprintf(output_file, "%s %s\n", tok.type.c_str(), tok.value.c_str());
            current_index++;
        } else {
            // 2. 匹配失败：报告错误并尝试恢复

            // 报告错误 i, j, k
            if (error_type == 'i') {
                ERROR_i(error_line);
            } else if (error_type == 'j') {
                ERROR_j(error_line);
            } else if (error_type == 'k') {
                ERROR_k(error_line);
            }
                // 否则（如果有其他错误类型传入，理论上不应该，但作为安全措施）
            else {
                fprintf(stderr, "Unexpected soft syntax error: Expected %s, got %s\n",
                        expected_type.c_str(), tok.type.c_str());
            }
            const char* token_value = "";
            if (expected_type == "SEMICN") {
                token_value = ";";
            } else if (expected_type == "RPARENT") {
                token_value = ")";
            } else if (expected_type == "RBRACK") {
                token_value = "]";
            }

            // **关键步骤：将缺失的 Token 类型和符号值写入 parser.txt**
            fprintf(output_file, "%s %s\n", expected_type.c_str(), token_value);
            // 3. 错误恢复策略:
            //    - 不消耗当前的错误 Token (current_index 不变)。
            //    - 允许解析器继续执行下一个匹配或非终结符的规则。
            //    - 因为Token不存在，所以不输出到 output_file。
        }
    }
    void print_non_terminal(const char* name) {
        fprintf(output_file, "<%s>\n", name);
    }
    IRValue ensure_i32(IRValue val) {
        if (val.type == "i32") return val;

        std::string res = ir_generator.new_reg();
        if (val.type == "i1") {
            // i1 -> i32 (Zero Extension)
            ir_generator.write_func(res + " = zext i1 " + val.name + " to i32");
        } else {
            // 处理其他情况，或者报错
            return {"0", "i32"};
        }
        return {res, "i32"};
    }
    // ------------------- 符号表/作用域管理 -------------------

    void enter_scope() {
        g_scope_counter++;
        g_active_scope_ids.push(g_scope_counter);
        g_scope_stack.push_back({});
    }

    void exit_scope() {
        g_scope_stack.pop_back();
        if (!g_active_scope_ids.empty()) {
            g_active_scope_ids.pop();
        }
    }
    // 检查当前作用域是否重复定义 (错误 b)
    bool check_redefinition(const std::string& name, int line) {
        if (g_scope_stack.back().count(name)) {
            ERROR_b(line);
            // 报告错误 b
            // ERROR_b(line);
            return true;
        }
        return false;
    }

    // 查找符号
    bool find_symbol(const std::string& name, Symbol& found_symbol) {
        for (auto it = g_scope_stack.rbegin(); it != g_scope_stack.rend(); ++it) {
            if (it->count(name)) {
                // 找到符号，将其信息存储到 found_symbol 中
                found_symbol = it->at(name);
                return true;
            }
        }
        if (g_builtin_symbols.count(name)) {
            found_symbol = g_builtin_symbols.at(name);
            return true;
        }
        return false;
    }
    Symbol& lookup_symbol(const std::string& name) {
        // 1. 查找作用域栈
        // 从最内层作用域（栈顶）开始向外查找
        for (auto it = g_scope_stack.rbegin(); it != g_scope_stack.rend(); ++it) {
            if (it->count(name)) {
                // 找到符号，返回其在 map 中的引用
                return it->at(name);
            }
        }

        // 2. 查找内置符号
        if (g_builtin_symbols.count(name)) {
            // 返回内置符号的引用
            return g_builtin_symbols.at(name);
        }

        // 3. 错误处理
        // 如果找不到，则抛出错误。这是编译器/Parser 应当处理的“未定义符号”错误。
        // 必须确保所有可能的执行路径都会返回一个 Symbol&
        throw std::runtime_error("Error: Undeclared identifier '" + name + "' used in parameter processing.");
    }
    // 添加符号到当前作用域
    void add_symbol(const std::string& name,  Symbol symbol, int line) {
        if (!check_redefinition(name, line)) {
            //symbol.scope_id = g_scope_counter;
            g_scope_stack.back()[name] = symbol;

            std::string type_name = infer_type_name(symbol); // 需要实现这个辅助函数
            g_symbol_output_records.push_back({
                                                      symbol.scope_id,
                                                      symbol.name,
                                                      type_name,
                                                      symbol.line_declared,
                                                      ++g_insert_id
                                              });
        }else{
            return;
        }
    }

    // 检查变量是否已定义 (错误 c)
    bool check_variable_declared(const std::string& name, int line) {
        Symbol temp;
        if (!find_symbol(name, temp)) {
            ERROR_c(line);
            return false;
        }else{
            return true;
        }
        //return true;
    }


    // ------------------- 核心递归下降函数 -------------------

    // BType -> 'int'
    void parseBType(std::string& type_out) {
        match("INTTK");
        type_out = "int";
        //print_non_terminal("BType");
    }



    // <CompUnit> -> {Decl} {FuncDef} MainFuncDef
    void parseCompUnit() {
        enter_scope(); // 进入全局作用域

        // 预置运行时库函数
//        add_symbol("getint", {"getint", "int", false, false, {}, 0, 1, 0, {}}, 0); // 返回 int, 0 个参数
//        add_symbol("printf", {"printf", "void", false, false, {}, 0, 1, -1, {}}, 0); // 返回 void, -1 表示参数可变或不检查

        while (current_token().type != "EOF") {
            const Token& T1 = current_token();
            const Token& T2 = peek(1);
            const Token& T3 = peek(2);

            // 1. 检查是否是 MainFuncDef 的开头 (int main( ) )
            if (T1.type == "INTTK" && T2.type == "MAINTK" && T3.type == "LPARENT") {
                break; // 找到了主函数，退出循环
            }

            // 2. 检查是否是 FuncDef 的开头 ( FuncType Ident ( ) )
            if ((T1.type == "INTTK" || T1.type == "VOIDTK") && T2.type == "IDENFR" && T3.type == "LPARENT") {
                parseFuncDef();
                continue;
            }

            // 3. 检查是否是 Decl 的开头 (ConstDecl or VarDecl)
            if (T1.type == "CONSTTK" || T1.type == "STATICTK" || T1.type == "INTTK") {
                parseDecl();
                continue;
            }

            // 无法识别，退出循环
            break;
        }

        parseMainFuncDef();
        print_non_terminal("CompUnit");

        exit_scope();
    }

    // Decl -> ConstDecl | VarDecl
    void parseDecl() {
        if (current_token().type == "CONSTTK") {
            parseConstDecl();
        } else { // VarDecl: [ 'static' ] BType VarDef ...
            parseVarDecl();
        }
        //print_non_terminal("Decl");
    }

    // ConstDecl -> 'const' BType ConstDef { ',' ConstDef } ';'
    void parseConstDecl() {
        match("CONSTTK");
        std::string type;
        parseBType(type); // 匹配 BType ('int')

        parseConstDef(type);
        while (current_token().type == "COMMA") {
            match("COMMA");
            parseConstDef(type);
        }
        match_with_error_check("SEMICN", 'i', peek(-1).line);
        print_non_terminal("ConstDecl");
    }

    // ConstDef -> Ident [ '[' ConstExp ']' ] '=' ConstInitVal
    void parseConstDef(const std::string& type) {
        Token ident_tok = current_token();
        match("IDENFR");

        std::vector<int> dimensions;
        int total_array_size = 1;
        while (current_token().type == "LBRACK") {
            match("LBRACK");
            bool old_ctx = is_const_context;
            is_const_context = true;
            parseConstExp();
            is_const_context = old_ctx;
            int dim_size = 0;
            if (!const_value_stack.empty()) {
                dim_size = const_value_stack.top().value;
                const_value_stack.pop();
                total_array_size *= dim_size; // 更新总大小
            } else {
                // 错误处理，如果 ConstExp 没产生值，可以默认大小为 1 或报错
                dim_size = 1;
            }
            dimensions.push_back(dim_size);
            match_with_error_check("RBRACK", 'k', peek(-1).line);
            //dimensions.push_back(0); // 占位
        }

        Symbol new_const = {ident_tok.value, type, true, false, dimensions, ident_tok.line};
        if (!g_active_scope_ids.empty()) {
            new_const.scope_id = g_active_scope_ids.top(); // <-- 使用当前活跃的作用域 ID (即 9)
        } else {
            // 错误处理，理论上不应该发生
            new_const.scope_id = 1;
        }
        if (!new_const.dimensions.empty()) { // 这是一个数组
            // 1. 设置 LLVM Name (全局变量名)
            std::string global_name;

            // 【修改点】区分全局和局部常量数组的命名
            if (new_const.scope_id == 1) {
                // 全局作用域：直接使用标识符
                global_name = "@" + ident_tok.value;
                new_const.is_global = true;
            } else {
                // 局部作用域：常量数组在 LLVM 中通常提升为全局常量数据，
                // 为了防止不同函数内定义了同名常量数组导致冲突，必须添加 scope_id 后缀
                global_name = "@" + ident_tok.value + "_" + std::to_string(new_const.scope_id);
            }
            new_const.llvm_name = global_name;

            // 2. 设置 LLVM Type (指向数组类型的指针)
            // 注意：total_array_size 已经在之前的 while 循环中计算完毕
            std::string array_ir_type = "[" + std::to_string(total_array_size) + " x i32]";
            new_const.llvm_type = array_ir_type + "*";

        } else { // 这是一个标量常量 (如 'const int a = 2')
            new_const.llvm_type = "i32"; // 标量的类型
            // 标量的值 (llvm_name) 将在 parseConstInitVal 后续逻辑中设置
        }
        //add_symbol(ident_tok.value, new_const, ident_tok.line);

        match("ASSIGN");
        size_t initial_stack_size = const_value_stack.size();
        is_const_context = true;
        parseConstInitVal();
        is_const_context = false;
        if (!new_const.dimensions.empty()) {
            // 1. 提取和反转初始化值 (精简处理 LIFO 栈)
            int num_values = const_value_stack.size() - initial_stack_size;
            std::vector<int> init_values;
            // LIFO 栈取出的是反序 (例如 3, 2, 1)
            for (int i = 0; i < num_values; ++i) {
                if (!const_value_stack.empty()) {
                    init_values.push_back(const_value_stack.top().value);
                    const_value_stack.pop();
                }
            }
            std::reverse(init_values.begin(), init_values.end()); // 反转为正序 (1, 2, 3)
            new_const.const_init_values.resize(total_array_size, 0); // 默认填0
            for (int i = 0; i < total_array_size && i < (int)init_values.size(); ++i) {
                new_const.const_init_values[i] = init_values[i];
            }
            // 2. 构造 LLVM 初始化列表字符串 (包括补零)
            std::string initializer_list_content;
            for (int i = 0; i < total_array_size; ++i) {
                if (i > 0) {
                    initializer_list_content += ", ";
                }
                // 如果初始化值不足 (例如 b[3]={1})，则用 0 补齐
                int val = 0;
                if (i < init_values.size()) {
                    val = init_values[i];
                }
                initializer_list_content += "i32 " + std::to_string(val);
            }

            // 3. 构造完整的 LLVM 'constant' 定义，并添加到全局 IR
            // 数组类型字符串是 new_const.llvm_type 去掉末尾的 "*"
            std::string array_ir_type_no_ptr = new_const.llvm_type.substr(0, new_const.llvm_type.length() - 1);
            std::string ir_def = new_const.llvm_name + " = constant " + array_ir_type_no_ptr + " [" + initializer_list_content + "], align 4\n";

            // 假设 g_ir_generator 是 IRGenerator 实例，且 add_global_ir() 存在
            ir_generator.write_global(ir_def);
        }
        // ---------------------------------------------------

        // 别忘了处理标量常量，将 ConstInitVal 推入的值取出
        if (new_const.dimensions.empty()) {
            // 如果是标量，我们需要从栈中拿到计算出的值 (如 10)
            std::string final_val_str = "0";
            if (!const_value_stack.empty()) {
                int val = const_value_stack.top().value;
                const_value_stack.pop(); // 弹出并使用
                final_val_str = std::to_string(val);
            }
            new_const.llvm_name = final_val_str; // 设置为 "0" 而不是 "@a1"

            // 重新更新到符号表 (覆盖旧的 @a1 定义)
//            if (!g_scope_stack.empty()) {
//                g_scope_stack.back()[ident_tok.value] = new_const;
//            }
        }
        add_symbol(ident_tok.value, new_const, ident_tok.line);
        print_non_terminal("ConstDef");
    }

    // ConstInitVal -> ConstExp | '{' [ ConstExp { ',' ConstExp } ] '}'
    void parseConstInitVal() {
        if (current_token().type == "LBRACE") {
            match("LBRACE");
            if (current_token().type != "RBRACE") {
                parseConstExp();
                while (current_token().type == "COMMA") {
                    match("COMMA");
                    parseConstExp();
                }
            }
            match("RBRACE");
        } else {
            parseConstExp();
        }
        print_non_terminal("ConstInitVal");
    }


    // VarDecl -> [ 'static' ] BType VarDef { ',' VarDef } ';'
    void parseVarDecl() {
        bool is_static = false;
        if (current_token().type == "STATICTK") {
            match("STATICTK");
            is_static = true;
        }

        std::string type;
        parseBType(type);

        parseVarDef(type, is_static);
        while (current_token().type == "COMMA") {
            match("COMMA");
            parseVarDef(type, is_static);
        }
        match_with_error_check("SEMICN", 'i', peek(-1).line);
        print_non_terminal("VarDecl");
    }
    IRValue parseConstInitValIR() {
        if (current_token().type == "LBRACE") {
            // 数组初始化：这里需要实现数组常量聚合体的求值，但目前可以先跳过
            match("LBRACE");
            if (current_token().type != "RBRACE") {
                // 对于数组初始化，递归调用 parseConstInitValIR 或 parseConstExp
                parseConstExp();
                while (current_token().type == "COMMA") {
                    match("COMMA");
                    parseConstExp();
                }
            }
            match_with_error_check("RBRACE", 'k', peek(-1).line);
            // 对于 int arr[3] = {1}; 这种，如果只解析到第一个元素，返回第一个元素的字面量
            return {"0", "i32"};
        } else if (current_token().type == "STRCON") {
            Token t = current_token();
            match("STRCON");
            // 全局字符串常量定义是合法的，这里保持调用 define_string
            print_non_terminal("ConstInitVal");
            // 对于数组常量初始化，返回 LLVM 的零初始化常量（或后续的聚合常量）
            return {"zeroinitializer", "i32"};
        } else {
            is_const_context = true;
            IRValue result = parseConstExp();
            is_const_context = false;
            print_non_terminal("ConstInitVal");
            // 【核心修改点 C: 强制调用 parseConstExp()】
            // 表达式：必须使用常量求值，返回字面量 (如 "3")
            return result;
        }
    }
    // VarDef -> Ident [ '[' ConstExp ']' ] [ '=' InitVal ]
    // 替换 compiler.cpp 中原有的 parseVarDef 函数
    // VarDef -> Ident [ '[' ConstExp ']' ] [ '=' InitVal ]
    void parseVarDef(const std::string& type, bool is_static) {
        Token ident_tok = current_token();
        match("IDENFR");

        std::vector<int> dimensions;
        while (current_token().type == "LBRACK") {
            match("LBRACK");
            IRValue array_size_ir = parseConstExp();
            int array_size = 0;
            try {
                array_size = std::stoi(array_size_ir.name);
            } catch (...) { array_size = 1; }
            match_with_error_check("RBRACK", 'k', peek(-1).line);
            dimensions.push_back(array_size);
        }

        Symbol new_var = {ident_tok.value, type, false, is_static, dimensions, ident_tok.line};
        if (!g_active_scope_ids.empty()) {
            new_var.scope_id = g_active_scope_ids.top();
        } else {
            new_var.scope_id = 1;
        }
        bool is_global = (new_var.scope_id == 1);
        new_var.is_global = is_global;

        // 计算数组总大小
        int total_size = 1;
        std::string array_ir_type = "";
        if (!dimensions.empty()) {
            for (int d : dimensions) total_size *= d;
            array_ir_type = "[" + std::to_string(total_size) + " x i32]";
            new_var.llvm_type = array_ir_type + "*";
        } else {
            new_var.llvm_type = "i32*";
        }

        // ----------------------------------------------------
        // 步骤 1: 定义变量 (分配空间)
        // ----------------------------------------------------
        if (is_global || new_var.is_const || is_static) {
            // 全局/静态变量：生成 global 定义
            if (is_global) {
                new_var.llvm_name = "@" + ident_tok.value;
            } else {
                new_var.llvm_name = "@" + ident_tok.value + "_" + std::to_string(new_var.scope_id);
            }
        } else {
            // 局部变量
            if (dimensions.empty()) {
                new_var.llvm_name = "%" + ident_tok.value + "_" + std::to_string(new_var.scope_id) + "_addr";
                std::string alloca_ir = "  " + new_var.llvm_name + " = alloca i32, align 4";
                if (!alloca_buffers.empty()) alloca_buffers.top() << alloca_ir << "\n";
                else ir_generator.write_alloca(alloca_ir);
            } else {
                new_var.llvm_name = "%arr_" + ident_tok.value + "_" + std::to_string(new_var.scope_id);
                std::string alloca_ir = "  " + new_var.llvm_name + " = alloca " + array_ir_type + ", align 4";
                if (!alloca_buffers.empty()) alloca_buffers.top() << alloca_ir << "\n";
                else ir_generator.write_alloca(alloca_ir);
            }
        }

        //add_symbol(ident_tok.value, new_var, ident_tok.line);

        // ----------------------------------------------------
        // 步骤 2: 处理初始化 (InitVal)
        // ----------------------------------------------------
        if (current_token().type == "ASSIGN") {
            match("ASSIGN");

            if (is_global || new_var.is_const || is_static) {
                // --- 编译时常量初始化 ---
                size_t initial_stack_size = const_value_stack.size();
                is_const_context = true;
                parseConstInitVal();
                is_const_context = false;

                std::string init_str = "0";
                if (!dimensions.empty()) {
                    // 数组常量初始化串
                    int num_values = const_value_stack.size() - initial_stack_size;
                    std::vector<int> init_values;
                    for(int i=0; i<num_values; ++i) {
                        if(!const_value_stack.empty()) {
                            init_values.push_back(const_value_stack.top().value);
                            const_value_stack.pop();
                        }
                    }
                    std::reverse(init_values.begin(), init_values.end());

                    std::stringstream ss;
                    ss << "[";
                    for (int i = 0; i < total_size; ++i) {
                        if (i > 0) ss << ", ";
                        int val = (i < (int)init_values.size()) ? init_values[i] : 0;
                        ss << "i32 " << val;
                    }
                    ss << "]";
                    init_str = ss.str();
                } else {
                    // 标量常量
                    if (!const_value_stack.empty()) {
                        init_str = std::to_string(const_value_stack.top().value);
                        const_value_stack.pop();
                    }
                }

                // 写入 Global 定义
                std::string storage_type = new_var.is_const ? "constant" : "global";
                std::string type_str = dimensions.empty() ? "i32" : array_ir_type;
                ir_generator.write_global(new_var.llvm_name + " = " + storage_type + " " + type_str + " " + init_str + ", align 4");

            } else {
                // --- 局部变量运行时初始化 ---
                if (dimensions.empty()) {
                    // 标量：解析表达式并 store
                    IRValue init_val = parseInitValIR();
                    ir_generator.write_func("store i32 " + init_val.name + ", i32* " + new_var.llvm_name + ", align 4");
                } else {
                    // 数组：解析 { exp, ... } 并逐个 store
                    if (current_token().type == "LBRACE") {
                        match("LBRACE");
                        int idx = 0;
                        if (current_token().type != "RBRACE") {
                            do {
                                // 解析每个元素表达式
                                IRValue val = parseExp();

                                // 生成 GEP 获取元素地址
                                std::string gep_reg = ir_generator.new_reg();
                                ir_generator.write_func(gep_reg + " = getelementptr inbounds " + array_ir_type + ", "
                                                        + array_ir_type + "* " + new_var.llvm_name + ", i32 0, i32 " + std::to_string(idx));

                                // 生成 store
                                ir_generator.write_func("store i32 " + val.name + ", i32* " + gep_reg + ", align 4");

                                idx++;
                                if (current_token().type == "COMMA") {
                                    match("COMMA");
                                } else {
                                    break;
                                }
                            } while (true);
                        }
                        match_with_error_check("RBRACE", 'k', peek(-1).line);
                    }
                    print_non_terminal("InitVal");
                }
            }
        } else {
            // 无显式初始化
            if (is_global || new_var.is_const || is_static) {
                std::string type_str = dimensions.empty() ? "i32" : array_ir_type;
                std::string init_str = dimensions.empty() ? "0" : "zeroinitializer";
                ir_generator.write_global(new_var.llvm_name + " = global " + type_str + " " + init_str + ", align 4");
            }
        }
        add_symbol(ident_tok.value, new_var, ident_tok.line);
        print_non_terminal("VarDef");
    }
    IRValue parseInitValIR() {
        // 支持三类： 1) 字符串 2) 花括号列表（暂不做数组逐元素赋值到 IR） 3) 单个表达式
        if (current_token().type == "LBRACE") {
            // 数组初始化（我们先做语义分析，IR 先简化为 0 占位）
            match("LBRACE");
            if (current_token().type != "RBRACE") {
                parseExp();
                while (current_token().type == "COMMA") {
                    match("COMMA");
                    parseExp();
                }
            }
            match_with_error_check("RBRACE", 'k', peek(-1).line);
            // 返回常量 0 作为临时值（用户如果要数组初始化应另行实现）
            print_non_terminal("InitVal");
            return {"0", "i32"};
        } else if (current_token().type == "STRCON") {
            Token t = current_token();
            match("STRCON");
            // 使用 IRGenerator 生成全局 string 常量并返回 i8*
            print_non_terminal("InitVal");
            return ir_generator.define_string(t.value);
        } else {
            // 普通表达式
            print_non_terminal("InitVal");
            return parseExp();
        }
        //print_non_terminal("InitVal");
    }

    // InitVal -> Exp | '{' [ Exp { ',' Exp } ] '}' | StringConst (STRCON)
    void parseInitVal() {
        if (current_token().type == "LBRACE") {
            match("LBRACE");
            if (current_token().type != "RBRACE") {
                parseExp();
                while (current_token().type == "COMMA") {
                    match("COMMA");
                    parseExp();
                }
            }
            match("RBRACE");
        } else if (current_token().type == "STRCON") {
            match("STRCON");
        } else {
            parseExp();
        }
        print_non_terminal("InitVal");
    }

    // FuncType -> 'void' | 'int'
    void parseFuncType(std::string& type_out) {
        if (current_token().type == "VOIDTK") {
            match("VOIDTK");
            type_out = "void";
        } else {
            match("INTTK");
            type_out = "int";
        }
        print_non_terminal("FuncType");
    }

    // FuncDef -> FuncType Ident '(' [FuncFParams] ')' Block
    void parseFuncDef() {
        std::string func_type;
        parseFuncType(func_type);

        Token ident_tok = current_token();
        match("IDENFR");

        // 1. 初始化 Symbol，并立即添加到全局作用域 (Scope 1)
        // 此时 func_type 尚未包含参数信息，但必须包含正确的返回类型。
        Symbol func_symbol = {
                ident_tok.value,
                func_type, // 【修正 2：使用 parseFuncType 得到的正确返回类型】
                false, // is_const: 函数名不是常量
                false, // is_static
                {},
                ident_tok.line,
                1, // scope_id 始终为 1
                0, // param_count 临时值
                {} // param_types 临时值
        };
        add_symbol(ident_tok.value, func_symbol, ident_tok.line);

        // 2. 保存外部状态并设置当前函数状态 (用于 G/F 错误追踪)
        std::string original_func_return_type = current_func_return_type;
        current_func_return_type = func_type;
//        bool outer_func_return_status = block_contains_return;
//        block_contains_return = false;

        match("LPARENT");

        // 3. 进入函数参数/局部变量作用域 (Scope 2)
        enter_scope(); // 【核心修正 1：只调用一次 enter_scope()】

        std::vector<ParamInfo> params_info;
        std::vector<std::string> param_types_for_global;

        // 4. 解析参数列表
        if (current_token().type == "INTTK") {
            params_info = parseFuncFParams();
        }
        std::string llvm_param_types_str;
        for (size_t i = 0; i < params_info.size(); ++i) {
            if (i > 0) llvm_param_types_str += ", ";
            // 数组参数在 LLVM IR 中是 i32* 类型，标量是 i32
            if (params_info[i].type == "int[]") {
                llvm_param_types_str += "i32*";
            } else {
                llvm_param_types_str += "i32";
            }
        }
// 2. 确定 LLVM 返回类型
        std::string ret_llvm_type = (func_type == "void") ? "void" : "i32";

// 3. 生成并写入 forward declare 语句
// 避免为 main 函数生成 declare，它只需要 define
//        if (ident_tok.value != "main") {
//            // 格式: declare <ret_type> @<func_name>(<param_types>)
//            std::string declare_ir = "declare " + ret_llvm_type + " @" + ident_tok.value + "(" + llvm_param_types_str + ")";
//            ir_generator.write_global(declare_ir); // 写入 global_ir 缓冲区，该缓冲区在文件顶部输出
//        }

// 4. 【可选】修正全局符号表中的参数类型信息
// 确保全局符号表中的函数信息是完整的，用于后续的 d/e 错误检查。
// 这一步必须在 enter_scope() 之后进行，因为参数 symbol 已经添加到内层作用域。
        Symbol& global_func_symbol = lookup_symbol(ident_tok.value);
        global_func_symbol.param_count = (int)params_info.size();
        global_func_symbol.param_types.clear();
        for (const auto& param : params_info) {
            global_func_symbol.param_types.push_back(param.type);
        }
        for (int i = 0; i < params_info.size(); ++i) {
            std::string param_name = params_info[i].name;
            std::string param_type = params_info[i].type;
            std::string param_reg = "%" + std::to_string(i); // LLVM 传入寄存器 %0, %1, ...

            // 从当前作用域查找 Symbol 引用
            Symbol& param_symbol = lookup_symbol(param_name);

            // 核心：设置 LLVM 名称，这是解决 GEP 报错的关键
            if (param_type == "int[]") {
                // 数组参数：直接使用传入的寄存器作为基地址（指针）
                param_symbol.llvm_name = param_reg;
            } else {
                // 标量参数 'int'：需要 alloca/store 来获取栈地址
                // ** 这里的逻辑需完善，但为解决编译错误，暂定如下 **
                // param_symbol.llvm_name = "TODO_ALLOCATE_" + param_reg; // 占位符
                param_symbol.llvm_name = param_reg;
            }

            // 收集用于更新全局符号表的类型字符串
            param_types_for_global.push_back(param_type);
        }
        // 5. 手动更新 Global Scope (Scope 1) 中该函数符号的参数信息
        if (!g_scope_stack.empty() && g_scope_stack[0].count(ident_tok.value)) {
            Symbol& global_symbol = g_scope_stack[0].at(ident_tok.value);
            global_symbol.param_count = param_types_for_global.size();
            global_symbol.param_types = param_types_for_global; // 存储类型列表
        }
        //std::string ret_llvm_type = (func_type == "void") ? "void" : "i32";

// ----------------------【IR GENERATION START】----------------------
// 1. 清空当前函数体的 IR 缓冲区，并重置寄存器计数
        ir_generator.clear_function_body_ir();
        ir_generator.reset_register_count();

// 2. 准备用于 define 头的参数列表字符串
        std::string llvm_param_names_str;
        for (size_t i = 0; i < params_info.size(); ++i) {
            if (i > 0) llvm_param_names_str += ", ";

            // **注意：** LLVM IR 的参数必须有类型和名称，例如 `i32 %a`
            std::string param_llvm_type = (params_info[i].type == "int[]") ? "i32*" : "i32";

            // 假设参数名称在 SymbolTable 中存储为 %param_name
            Symbol& param_sym = lookup_symbol(params_info[i].name);

            // %arg1 是参数的值寄存器。param_sym.llvm_name 应该存储的是分配的栈地址（%a_addr）
            std::string arg_reg_name = "%arg" + std::to_string(i + 1); // 临时参数值寄存器名

            llvm_param_names_str += param_llvm_type + " " + arg_reg_name;

            // **重要：** 需要将这个临时寄存器名（参数的传入值）存储起来，用于后续的 alloca/store 步骤
            param_sym.llvm_name = arg_reg_name; // 暂时用 llvm_name 存储传入的寄存器名
        }


// 3. 生成 define 头部和 entry 块
        std::string func_header_ir = "define " + ret_llvm_type + " @" + ident_tok.value + "(" + llvm_param_names_str + ") {\n";
        ir_generator.write_func(func_header_ir);
        ir_generator.write_func("entry:");

// 4. 为函数参数生成 alloca 和 store 指令
// 这必须在 entry 块的开头完成，以确保参数可寻址
        alloca_buffers.push(std::stringstream()); // 为本函数创建 alloca 缓冲区
        for (size_t i = 0; i < params_info.size(); ++i) {
            // 重新查找符号，获取其在 parseFuncFParams 中确定的最终栈地址 llvm_name
            Symbol& param_sym = lookup_symbol(params_info[i].name);

            // 生成 alloca
            std::string param_llvm_type = (params_info[i].type == "int[]") ? "i32*" : "i32";
            std::string alloca_reg = ir_generator.new_reg();
            // 如果是数组参数，我们需要为指针分配空间
            if (params_info[i].type == "int[]") {
                alloca_buffers.top() << "  " << alloca_reg << " = alloca i32*, align 8\n";
            } else { // 标量参数
                alloca_buffers.top() << "  " << alloca_reg << " = alloca i32, align 4\n";
            }

            // 将参数的栈地址更新为 alloca 结果
            std::string incoming_reg = param_sym.llvm_name; // 之前存储的传入寄存器名
            param_sym.llvm_name = alloca_reg; // 符号表的 llvm_name 应该存储栈地址

            // 生成 store (将传入的值存入栈地址)
            ir_generator.write_func("  store " + param_llvm_type + " " + incoming_reg + ", " + param_llvm_type + "* " + param_sym.llvm_name + ", align 4");
        }
        match_with_error_check("RPARENT", 'j', ident_tok.line);
        basic_block_terminated = false;
        parseBlock(true);
//        int rbrace_line = peek(-1).line;
//
//        // 6. G 错误检查
//        if (func_type == "int" && !block_contains_return) {
//            ERROR_g(rbrace_line);
//        }
        if (!basic_block_terminated) {
            if (func_type == "void") {
                ir_generator.write_func("  ret void");
            } else {
                // 对于 int 函数，如果控制流到了末尾却没有 return，补一个 0 防止报错
                ir_generator.write_func("  ret i32 0");
            }
        }
        // 7. 退出作用域并恢复外部状态
        exit_scope(); // 【核心修正 1：与 enter_scope() 配对】
        std::string alloca_content = alloca_buffers.top().str();
        alloca_buffers.pop();
        //std::string body_content = ir_generator.get_and_clear_function_body_ir();

// 2. 重新组合完整的函数 IR 字符串
// 注意：function_ir 当前包含了 define 头部和 entry: 标签
        std::string full_func_ir = ir_generator.get_and_clear_function_body_ir(); // <--- 修正: 使用 get_and_clear_function_body_ir()

// 插入 alloca 语句 (必须在 entry: 之后，其他指令之前)
// 找到 "entry:\n" 的位置
        size_t entry_pos = full_func_ir.find("entry:\n");
        if (entry_pos != std::string::npos) {
            // 插入 alloca_content (注意: 如果 alloca_content 已经包含了换行，这里可能需要调整)
            full_func_ir.insert(entry_pos + 7, alloca_content); // 7 = "entry:\n" 的长度
        }

// 3. 检查是否有返回指令，如果没有，为 `void` 函数添加默认 `ret void`
//        if (ident_tok.value == "main" && full_func_ir.find("ret i32") == std::string::npos) {
//            full_func_ir += "  ret i32 0\n";
//        } else if (func_type == "void" && full_func_ir.find("ret ") == std::string::npos) {
//            full_func_ir += "  ret void\n";
//        }
// 4. 添加函数结束符
        full_func_ir += "}\n";

// 5. 将完整的函数定义写入全局 IR 流
        ir_generator.write_global(full_func_ir);
        current_func_return_type = original_func_return_type; // 恢复到调用前的返回类型状态
        //block_contains_return = outer_func_return_status;

        print_non_terminal("FuncDef");
    }

    // MainFuncDef -> 'int' 'main' '(' ')' Block
    void parseMainFuncDef() {
        match("INTTK");
        match("MAINTK");
        std::string original_return_type = current_func_return_type;
        current_func_return_type = "int";

        // 创建一个 alloca buffer 用于收集 main 的 alloca (存储在 alloca_buffers.top())
        alloca_buffers.push(std::stringstream());

        // 创建一个 body buffer 用于收集函数体指令 (存储在 ir_generator.function_ir_body 中)
        // 假设您的 ir_generator 有一个方法来开启/清空/关闭函数体缓冲区

        // ！！！ 关键：在解析函数体之前，清空函数体IR缓冲区，确保它是空的 ！！！
        ir_generator.clear_function_body_ir();

        // 匹配括号
        match("LPARENT");
        match_with_error_check("RPARENT", 'j', peek(-1).line);
        ir_generator.reset_register_count();
        // 1. 解析函数体，所有 store, load, call 等指令将写入 function_ir_body 缓冲区
        enter_scope();
        basic_block_terminated = false;
        bool has_return = parseBlock(true);

        // 2. 取得 alloca 内容
        std::string alloca_content = alloca_buffers.top().str();
        alloca_buffers.pop();

        // 3. 取得函数体指令内容
        // 假设 ir_generator.get_function_body_ir() 返回 function_ir_body 的内容并清空
        std::string body_content = ir_generator.get_function_body_ir();

        // ------------------------------------------------------------------
        // 4. 组合 IR 并一次性写入全局流 (使用 write_global 确保是顶层实体)
        // ------------------------------------------------------------------

        // I. 写入函数头
        ir_generator.write_global("define i32 @main() {");
        ir_generator.write_global("entry:");

        // II. 写入 alloca (必须在 entry 标签下，且在 body 之前)
        // 这一步取代了您原代码中的循环追加 alloca
        if (!alloca_content.empty()) {
            ir_generator.write_global(alloca_content); // alloca_content 应该包含缩进
        }

        // III. 写入函数体指令 (Body Content)
        if (!body_content.empty()) {
            // body_content 中包含 store, load 等指令
            ir_generator.write_global(body_content);
        }

        // IV. 写入终结指令 ret
        int rbrace_line = peek(-1).line;
        if (!has_return) {
            ERROR_g(rbrace_line);
            ir_generator.write_global("  ret i32 0");
        } else {
            // 如果有 return 语句，parseBlock 应该已经写入了 ret 指令
            // 但如果您的 IR 生成逻辑是依赖 parseBlock 结束后再添加 ret，这里需要调整。
            // 为了安全，假设 parseBlock 负责写入最后的 ret，除非没有显式 return。
        }

        // V. 写入函数结束 }
        ir_generator.write_global("}"); // *** 关键：用 write_global 写入，确保 } 是顶层实体 ***

        current_func_return_type = original_return_type;
        exit_scope();
        print_non_terminal("MainFuncDef");
    }

    struct ParamInfo {
        std::string name;
        std::string type;
    };
    // FuncFParams -> FuncFParam { ',' FuncFParam }
    std::vector<ParamInfo> parseFuncFParams() {
        std::vector<ParamInfo> params; // Collect types
        params.push_back(parseFuncFParam()); // 捕获第一个参数的类型

        while (current_token().type == "COMMA") {
            match("COMMA");
            params.push_back(parseFuncFParam()); // 捕获后续参数的类型
        }
        print_non_terminal("FuncFParams");
        return params; // 【关键】返回类型列表
    }

    // FuncFParam -> BType Ident ['[' ']']
    ParamInfo parseFuncFParam() {
        std::string type;
        parseBType(type);

        Token ident_tok = current_token();
        match("IDENFR");

        std::vector<int> dimensions;
        bool is_array_param = false;
        if (current_token().type == "LBRACK") {
            match("LBRACK");
            match_with_error_check("RBRACK", 'k', peek(-1).line);
            dimensions.push_back(0); // 标识为数组指针
            is_array_param = true;
            while (current_token().type == "LBRACK") {
                match("LBRACK");
                // 这里必须计算出常量值
                bool old_ctx = is_const_context;
                is_const_context = true;
                IRValue dim_ir = parseConstExp();
                is_const_context = old_ctx;

                int dim_val = 1;
                try { dim_val = std::stoi(dim_ir.name); } catch(...) {}
                dimensions.push_back(dim_val);

                match_with_error_check("RBRACK", 'k', peek(-1).line);
            }
        }

        Symbol param_symbol = {ident_tok.value, type, false, false, dimensions, ident_tok.line};
        param_symbol.scope_id = g_scope_counter;
        param_symbol.is_param = true;
        param_symbol.llvm_name = "%arg_" + ident_tok.value;
        add_symbol(ident_tok.value, param_symbol, ident_tok.line);
        std::string type_str = is_array_param ? "int[]" : "int";
        print_non_terminal("FuncFParam");
        return {ident_tok.value, type_str};
    }

    // Block -> '{' { BlockItem } '}'
    bool parseBlock(bool is_func_body) {
        match("LBRACE");

        // 引入局部变量追踪该块是否保证返回
        bool block_guarantees_return = false;

        // 移除旧的状态保存和重置

        while (current_token().type != "RBRACE" && current_token().type != "EOF") {
            const Token& T1 = current_token();

            if (T1.type == "CONSTTK" || T1.type == "STATICTK" || T1.type == "INTTK") {
                // BlockItem -> Decl
                parseDecl();
            } else {
                // BlockItem -> Stmt
                // 核心：调用 parseStmt 并检查其返回值
                if (parseStmt()) {
                    block_guarantees_return = true;
                }
            }
        }

        const Token& rbrace_tok = current_token();

        // 普遍性 G 错误检查：仅在是函数体、返回 int 且不保证返回时报错
        if (is_func_body && current_func_return_type == "int" && !block_guarantees_return) {
            ERROR_g(rbrace_tok.line); // 报告 '}' 所在的行号
        }

        match("RBRACE");

        // 移除旧的状态恢复

        print_non_terminal("Block");
        return block_guarantees_return; // 返回该块是否保证返回
    }

    // BlockItem -> Decl | Stmt
    void parseBlockItem() {
        if (current_token().type == "CONSTTK" || current_token().type == "STATICTK" || current_token().type == "INTTK") {
            parseDecl();
        } else {
            parseStmt();
        }
        //print_non_terminal("BlockItem");
    }

    // Stmt 规则 (注意 Block 需创建新作用域，LVal 需前瞻)
    bool parseStmt() {
        const Token& T1 = current_token();

        if (T1.type == "LBRACE") {
            enter_scope();
            // 传入 false，这不是函数体块
            bool block_guarantees_return = parseBlock(false);
            exit_scope();
            print_non_terminal("Stmt");
            return block_guarantees_return; // 返回子块的状态
        } else if (T1.type == "IFTK") {
            match("IFTK");
            match("LPARENT");
            IRValue cond_val =parseCond();
            match_with_error_check("RPARENT", 'j', peek(-1).line);

            // 声明局部变量，用于追踪 if 和 else 分支是否保证返回
            std::string true_label = ir_generator.new_label("if_then");
            std::string false_label = ir_generator.new_label("if_else");
            std::string merge_label = ir_generator.new_label("if_merge");
            bool then_block_terminated = false;
            bool else_block_terminated = false;
            // --- IR Generation Step 3: 终止当前块，跳转到 true/false 分支 ---
            // 必须在执行 if 语句体之前，用条件分支指令 `br i1` 结束当前基本块。
            ir_generator.write_func("br i1 " + cond_val.name + ", label %" + true_label + ", label %" + false_label);

            // 声明局部变量，用于追踪 if 和 else 分支是否保证返回
            // 4.1 开始 'then' 块
            ir_generator.write_func("\n" + true_label + ":");
            basic_block_terminated = false;
            bool if_branch_guarantees = parseStmt(); // 生成 if (then) 语句体 IR
            then_block_terminated = basic_block_terminated;
            // 4.2 如果 'then' 块没有返回，则无条件分支到合并块
            if (!then_block_terminated) {
                ir_generator.write_func("br label %" + merge_label);
            }

            // 5.1 开始 'else' 块
            ir_generator.write_func("\n" + false_label + ":");
            basic_block_terminated = false;
            bool else_branch_guarantees = false;

            // 5.2 解析 'else'
            if (current_token().type == "ELSETK") {
                match("ELSETK");
                else_branch_guarantees = parseStmt(); // 生成 else 语句体 IR
                else_block_terminated = basic_block_terminated;
            }
            if (!basic_block_terminated) {
                ir_generator.write_func("br label %" + merge_label);
            }
            // 5.3 如果 'else' 块（或没有 else 时的 fall-through 路径）没有返回，则无条件分支到合并块
//            if (!else_branch_guarantees) {
//                ir_generator.write_func("br label %" + merge_label);
//            }

            ir_generator.write_func("\n" + merge_label + ":");
//            std::string dummy_reg = ir_generator.new_reg();
//            ir_generator.write_func(dummy_reg + " = add i32 0, 0");
            basic_block_terminated = false;
            print_non_terminal("Stmt");

            // 核心：只有 if 和 else 分支都保证返回时，整个 if 语句才保证返回
            return if_branch_guarantees && else_branch_guarantees;
        } else if (T1.type == "FORTK") {
            // ... (FOR 循环的解析代码不变) ...
            match("FORTK");
            match("LPARENT");
            if (current_token().type != "SEMICN") {
                parseForStmt();
            }// [ForStmt]
            match("SEMICN");
            std::string cond_label = ir_generator.new_label("for_cond");
            std::string body_label = ir_generator.new_label("for_body");
            std::string inc_label  = ir_generator.new_label("for_inc");
            std::string end_label  = ir_generator.new_label("for_end");
            loop_inc_labels.push(inc_label);
            loop_end_labels.push(end_label);
            ir_generator.write_func("br label %" + cond_label);

            // 3. Cond (条件判断块)
            ir_generator.write_func("\n" + cond_label + ":");
            if (current_token().type != "SEMICN") {
                IRValue cond_val =parseCond();
                ir_generator.write_func("br i1 " + cond_val.name + ", label %" + body_label + ", label %" + end_label);
            }else {
                // 空条件默认为真，直接跳 body
                ir_generator.write_func("br label %" + body_label);
            }// [Cond]
            match("SEMICN");
            size_t inc_start_index = current_index;
            int paren_depth = 0;
            while (current_index < g_tokens.size()) {
                if (g_tokens[current_index].type == "LPARENT") {
                    paren_depth++;
                } else if (g_tokens[current_index].type == "RPARENT") {
                    if (paren_depth == 0) {
                        break; // 找到了 for 循环结束的括号
                    }
                    paren_depth--;
                }
                current_index++;
            } // [ForStmt]
            match_with_error_check("RPARENT", 'j', peek(-1).line);
            ir_generator.write_func("\n" + body_label + ":");
            loop_depth_counter++;
            basic_block_terminated = false;
            parseStmt();
            loop_depth_counter--;
            if (!basic_block_terminated) {
                ir_generator.write_func("br label %" + inc_label);
            }
            basic_block_terminated = false;
            //ir_generator.write_func("br label %" + inc_label);
            ir_generator.write_func("\n" + inc_label + ":");

            size_t body_end_index = current_index;
            current_index = inc_start_index;
            if (current_token().type != "RPARENT") {
                parseForStmt(); // 解析 Inc，此时生成的 IR 编号是递增后的正确编号
            }
            // Inc 执行完跳转回 Cond
            ir_generator.write_func("br label %" + cond_label);
            current_index = body_end_index;
            // 7. End (结束块)
            ir_generator.write_func("\n" + end_label + ":");
            loop_inc_labels.pop();
            loop_end_labels.pop();
            print_non_terminal("Stmt");
            return false; // FOR 循环不保证返回
        } else if (T1.type == "BREAKTK") {
            // ... (BREAK 的解析代码不变) ...
            int line = T1.line;
            match("BREAKTK");
            if (loop_depth_counter == 0) {
                ERROR_m(line);
            }
            match_with_error_check("SEMICN", 'i', line);
            if (!loop_end_labels.empty()) {
                ir_generator.write_func("br label %" + loop_end_labels.top());
            }
            print_non_terminal("Stmt");
            basic_block_terminated = true;
            return false; // BREAK 不保证返回
        } else if (T1.type == "CONTINUETK") {
            // ... (CONTINUE 的解析代码不变) ...
            int line = T1.line;
            match("CONTINUETK");
            if (loop_depth_counter == 0) {
                ERROR_m(line);
            }
            match_with_error_check("SEMICN", 'i', line);
            if (!loop_inc_labels.empty()) {
                ir_generator.write_func("br label %" + loop_inc_labels.top());
            }
            print_non_terminal("Stmt");
            basic_block_terminated = true;
            return false; // CONTINUE 不保证返回
        } else if (T1.type == "RETURNTK") {
            Token return_tok = current_token();
            match("RETURNTK");
            bool has_return_value = false;
            if (current_token().type != "SEMICN") {
                IRValue ret_val =parseExp();
                ir_generator.write_func("ret i32 " + ret_val.name);
                has_return_value=true;
            }else {
                // **IR Generation: ret void**
                ir_generator.write_func("ret void");
            }
            if (current_func_return_type == "void" && has_return_value) {
                ERROR_f(return_tok.line);
            }
            // 移除旧的 block_contains_return = true;
            match_with_error_check("SEMICN", 'i', peek(-1).line);
            print_non_terminal("Stmt");
            basic_block_terminated = true;
            return true; // RETURNTK 保证返回
        } else if (T1.type == "PRINTFTK") {
            parsePrintfStmt();
            print_non_terminal("Stmt");
            return false; // PRINTF 不保证返回
        }
            // --- LVal='Exp';  VS  LVal=='Exp';  VS  [Exp]';' ---
        else {
            // ... (原有的赋值和表达式语句解析逻辑) ...
            const Token& T_start = current_token();

            // 1. **【分支 1：LVal 开头的高级前瞻】**
            if (T_start.type == "IDENFR") {

                size_t lookahead_index = current_index + 1;
                while (lookahead_index < g_tokens.size()) {
                    const std::string& type = g_tokens[lookahead_index].type;
                    if (type == "LBRACK" || type == "RBRACK" || type == "INTCON" || type == "IDENFR" || type == "LPARENT" || type == "RPARENT" || type == "PLUS" || type == "MINU" || type == "MULT" || type == "DIV" || type == "MOD"||type == "COMMA") {
                        lookahead_index++;
                    } else {
                        break;
                    }
                }

                if (lookahead_index < g_tokens.size()) {
                    const std::string& next_type = g_tokens[lookahead_index].type;

                    // b. **识别 ASSIGN**
                    if (next_type == "ASSIGN") {
                        IRValue dest_addr =parseLVal(true);
                        match("ASSIGN");
                        IRValue src_val=parseExp();
                        ir_generator.write_func(
                                "store i32 " + src_val.name + ", i32* " + dest_addr.name + ", align 4"
                        );
                        match_with_error_check("SEMICN", 'i', peek(-1).line);
                        print_non_terminal("Stmt");
                        return false; // 赋值不保证返回
                    }

                        // c. **【识别高级表达式语句】**
                    else if (next_type == "EQL" || next_type == "NEQ" || next_type == "LSS" ||
                             next_type == "GRE" || next_type == "LEQ" || next_type == "GEQ" ||
                             next_type == "AND" || next_type == "OR")
                    {
                        parseCond();
                        match_with_error_check("SEMICN", 'i', peek(-1).line);
                        print_non_terminal("Stmt");
                        return false; // 表达式不保证返回
                    }
                }


                // 2. **【分支 2：表达式语句 [Exp] ';' 或其他错误】**
                int line_for_error = T_start.line;
                size_t start_index = current_index;

                if (current_token().type != "SEMICN") {
                    parseExp();
                    line_for_error = peek(-1).line;
                }

                match_with_error_check("SEMICN", 'i', line_for_error);

                if (current_index == start_index &&
                    current_token().type != "SEMICN" &&
                    current_token().type != "RBRACE" &&
                    current_token().type != "EOF")
                {
                    current_index++;
                }

                print_non_terminal("Stmt");
                return false; // 表达式语句不保证返回
            }
            else {
                // 其他开头的 Exp; 语句
                int line_for_error = T_start.line;
                size_t start_index = current_index;

                if (current_token().type != "SEMICN") {
                    parseExp();
                    line_for_error = peek(-1).line;
                }

                match_with_error_check("SEMICN", 'i', line_for_error);

                if (current_index == start_index &&
                    current_token().type != "SEMICN" &&
                    current_token().type != "RBRACE" &&
                    current_token().type != "EOF")
                {
                    current_index++;
                }

                print_non_terminal("Stmt");
                return false; // 表达式语句不保证返回
            }
        }
    }

    // ForStmt -> LVal '=' Exp { ',' LVal '=' Exp }
    void parseForStmt() {
        IRValue dest =parseLVal(true);
        match("ASSIGN");
        IRValue src =parseExp();
        ir_generator.write_func("store i32 " + src.name + ", i32* " + dest.name + ", align 4");
        while (current_token().type == "COMMA") {
            match("COMMA");
            dest =parseLVal(true);
            match("ASSIGN");
            src = parseExp();
            ir_generator.write_func("store i32 " + src.name + ", i32* " + dest.name + ", align 4");
        }
        print_non_terminal("ForStmt");
    }

    // Printf 语句
    void parsePrintfStmt() {
        match("PRINTFTK");
        match("LPARENT");

        // 用于收集表达式的 IRValue
        std::vector<IRValue> exp_list;
        Token strcon_tok = current_token();

        // 1. 处理格式字符串和表达式列表
        match_with_error_check("STRCON", 'a', peek(-1).line); // A 错误检查

        // 统计格式字符串中 %d 的数量
        std::string format_str_value = strcon_tok.value;
        int format_count = 0;
        for (size_t i = 0; i < format_str_value.length(); ++i) {
            if (format_str_value[i] == '%' && i + 1 < format_str_value.length() && format_str_value[i + 1] == 'd') {
                format_count++;
            }
        }

        // 2. 处理后续 Exp
        while (current_token().type == "COMMA") {
            match("COMMA");
            exp_list.push_back(parseExp()); // <-- 确保调用 IRValue 版本
        }

        // 【L 错误检查】: 检查 %d 数量和 Exp 数量是否一致
        if (format_count != exp_list.size()) {
            ERROR_l(strcon_tok.line);
        }

        match_with_error_check("RPARENT", 'j', peek(-1).line);
        match_with_error_check("SEMICN", 'i', peek(-1).line);

        // **IR Generation: Printf Logic**

        std::string current_str = ""; // 用于构建要输出的子字符串
        int exp_index = 0; // 用于追踪 exp_list 索引

        // 3. 逐字符扫描格式字符串，混合生成 putstr 和 putint
        // 格式字符串是带双引号的，所以从 [1] 扫描到 [end-1]
        for (size_t i = 1; i < format_str_value.length() - 1; ++i) {
            if (format_str_value[i] == '%' && i + 1 < format_str_value.length() - 1 && format_str_value[i + 1] == 'd') {

                // a. 遇到 %d：先输出之前积累的字符串
                if (!current_str.empty()) {
                    // 定义字符串常量并调用 putstr
                    std::string temp_str_val = "\"" + current_str + "\"";
                    IRValue temp_str_ptr = ir_generator.define_string(temp_str_val);
                    ir_generator.write_func("call void @putstr(i8* " + temp_str_ptr.name + ")");
                    current_str.clear();
                }

                // b. 输出整数（如果表达式数量足够）
                if (exp_index < exp_list.size()) {
                    const auto& exp_val = exp_list[exp_index];
                    // 调用 putint 输出整数
                    ir_generator.write_func("call void @putint(i32 " + exp_val.name + ")");
                    exp_index++;
                }

                i++; // 跳过 'd' 字符

            } else if (format_str_value[i] == '\\' && i + 1 < format_str_value.length() - 1 && format_str_value[i + 1] == 'n') {
                // c. 处理换行符 \n
                current_str += '\n';
                i++; // 跳过 'n' 字符

            } else {
                // d. 积累普通字符
                current_str += format_str_value[i];
            }
        }

        // 4. 输出格式字符串末尾积累的字符串（如果有）
        if (!current_str.empty()) {
            std::string temp_str_val = "\"" + current_str + "\"";
            IRValue temp_str_ptr = ir_generator.define_string(temp_str_val);
            ir_generator.write_func("call void @putstr(i8* " + temp_str_ptr.name + ")");
        }

        print_non_terminal("Stmt"); // printf 也是一种 Stmt
    }

    // LVal -> Ident ['[' Exp ']']
    // 替换 compiler.cpp 中原有的 parseLVal 函数
    // LVal -> Ident ['[' Exp ']']
    // LVal -> Ident ['[' Exp ']']
    IRValue parseLVal(bool need_address) {
        Token ident_tok = current_token();
        match("IDENFR");

        Symbol var_symbol;
        bool declared = find_symbol(ident_tok.value, var_symbol);
        if (!declared) {
            ERROR_c(ident_tok.line);
            return {"0", "i32"};
        } else {
            if (need_address && var_symbol.is_const) {
                ERROR_h(ident_tok.line);
            }
        }

        // 1. 常量计算上下文处理
        if (is_const_context) {
            std::vector<int> indices;
            while (current_token().type == "LBRACK") {
                match("LBRACK");
                parseExp();
                int idx_val = 0;
                if (!const_value_stack.empty()) {
                    idx_val = const_value_stack.top().value;
                    const_value_stack.pop();
                }
                indices.push_back(idx_val);
                match_with_error_check("RBRACK", 'k', peek(-1).line);
            }

            if (var_symbol.is_const) {
                if (var_symbol.dimensions.empty()) {
                    return {var_symbol.llvm_name, "i32"};
                } else {
                    int flat_idx = 0;
                    for(size_t i = 0; i < indices.size(); ++i) {
                        int stride = 1;
                        for(size_t k = i + 1; k < var_symbol.dimensions.size(); k ++) {
                            stride *= var_symbol.dimensions[k];
                        }
                        flat_idx += indices[i] * stride;
                    }

                    int val = 0;
                    if (flat_idx >= 0 && flat_idx < (int)var_symbol.const_init_values.size()) {
                        val = var_symbol.const_init_values[flat_idx];
                    }
                    return {std::to_string(val), "i32"};
                }
            }
            return {"0", "i32"};
        }

        // 2. 运行时 IR 生成逻辑

        // 处理函数名作为左值（如 if(func)）的情况
        if (var_symbol.param_count >= 0) {
            return {var_symbol.llvm_name, "i32*"};
        }

        if (var_symbol.is_const) {
            if (!need_address && var_symbol.dimensions.empty() && current_token().type != "LBRACK") {
                if (var_symbol.llvm_name.empty()) return {"0", "i32"};
                return {var_symbol.llvm_name, "i32"};
            }
        }

        std::string base_ptr = var_symbol.llvm_name;
        bool skip_base_gep = false;

        if (var_symbol.is_param) {
            if (!var_symbol.dimensions.empty() || current_token().type == "LBRACK") {
                std::string loaded_ptr_reg = ir_generator.new_reg();
                ir_generator.write_func("  " + loaded_ptr_reg + " = load i32*, i32** " + base_ptr + ", align 4");
                base_ptr = loaded_ptr_reg;
            }
        }

        if (!var_symbol.dimensions.empty() && !var_symbol.is_param) {
            if (var_symbol.llvm_type.empty() || var_symbol.llvm_type.back() != '*') {
                skip_base_gep = true;
            } else {
                std::string gep_reg = ir_generator.new_reg();
                std::string base_type = var_symbol.llvm_type.substr(0, var_symbol.llvm_type.length() - 1);
                ir_generator.write_func("  " + gep_reg + " = getelementptr inbounds "
                                        + base_type + ", " + var_symbol.llvm_type + " "
                                        + base_ptr + ", i32 0, i32 0");
                base_ptr = gep_reg;
            }
        }

        IRValue total_offset = {"0", "i32"};
        bool has_index = false;
        int current_dim = 0; // 记录当前解析到了第几维

        while (current_token().type == "LBRACK") {
            match("LBRACK");
            IRValue current_idx = parseExp();
            match_with_error_check("RBRACK", 'k', peek(-1).line);
            has_index = true;

            int stride = 1;
            for (size_t k = current_dim + 1; k < var_symbol.dimensions.size(); ++k) {
                stride *= var_symbol.dimensions[k];
            }

            std::string stride_str = std::to_string(stride);
            std::string tmp_mul = ir_generator.new_reg();
            ir_generator.write_func(tmp_mul + " = mul i32 " + current_idx.name + ", " + stride_str);

            std::string tmp_add = ir_generator.new_reg();
            ir_generator.write_func(tmp_add + " = add i32 " + total_offset.name + ", " + tmp_mul);

            total_offset = {tmp_add, "i32"};
            current_dim++;
        }

        print_non_terminal("LVal");

        if (need_address) {
            // 如果需要地址（如赋值语句左侧 scanf），返回指针
            if (has_index || !var_symbol.dimensions.empty()) {
                std::string final_ptr = ir_generator.new_reg();
                ir_generator.write_func(final_ptr + " = getelementptr inbounds i32, i32* " + base_ptr + ", i32 " + total_offset.name);
                return {final_ptr, "i32*"};
            }
            return {base_ptr, "i32*"};
        } else {
            if (skip_base_gep && !has_index) {
                return {"0", "i32"};
            }

            // 【关键修改】判断是否是数组切片（未完全索引的数组）
            // 如果是数组且提供的索引数小于维数，说明是传参行为，应该返回地址而不是值
            bool is_array_slice = !var_symbol.dimensions.empty() && (current_dim < (int)var_symbol.dimensions.size());

            if (is_array_slice) {
                // 返回地址 (i32*)
                std::string final_ptr = base_ptr;
                if (has_index) {
                    final_ptr = ir_generator.new_reg();
                    ir_generator.write_func(final_ptr + " = getelementptr inbounds i32, i32* " + base_ptr + ", i32 " + total_offset.name);
                }
                return {final_ptr, "i32*"};
            }

            // 否则，是完全索引（访问标量），生成 load 指令
            std::string final_ptr = base_ptr;
            if (has_index) {
                final_ptr = ir_generator.new_reg();
                ir_generator.write_func(final_ptr + " = getelementptr inbounds i32, i32* " + base_ptr + ", i32 " + total_offset.name);
            }

            std::string val_reg = ir_generator.new_reg();
            ir_generator.write_func("  " + val_reg + " = load i32, i32* " + final_ptr + ", align 4");
            return {val_reg, "i32"};
        }
    }

    // Exp -> AddExp
    IRValue parseExp() {
        IRValue result = parseAddExp();
        print_non_terminal("Exp");
        return result;
    }

    // Cond -> LOrExp
    IRValue parseCond() {
        // 确保 parseLOrExp 返回的是 IRValue，其中 cond_result.name 是寄存器名（如 "%10"）
        IRValue cond_result = parseLOrExp();
        print_non_terminal("Cond");
        return cond_result; // <-- 返回包含寄存器名和类型（应为 "i1"）的 IRValue
    }

    // ConstExp -> AddExp (涉及的 Ident 必须是常量)
    IRValue parseConstExp() {
        IRValue result = parseAddExp(); // 语法结构与 AddExp 相同
        print_non_terminal("ConstExp");
        return result;
    }

    // ------------------- 消除左递归的表达式 (核心修改) -------------------
    IRValue convert_to_i1(IRValue val) {
        if (val.type == "i1") {
            return val;
        }
        // i32 转换为 i1：与 0 进行不等比较 (ne)
        std::string result_reg = ir_generator.new_reg();
        if (val.type == "i32*") {
            // 指针判空： icmp ne i32* %ptr, null
            ir_generator.write_func(result_reg + " = icmp ne i32* " + val.name + ", null");
        } else {
            // 整数判非零： icmp ne i32 %val, 0
            // 确保输入是 i32 (虽然通常 convert_to_i1 的输入已经是 i32，但安全起见)
            if (val.type != "i32") val = ensure_i32(val);
            ir_generator.write_func(result_reg + " = icmp ne i32 " + val.name + ", 0");
        }
        return IRValue {result_reg, "i1"};
    }
    // LOrExp -> LAndExp { '||' LAndExp }
    IRValue parseLOrExp() {
        // 1. 解析左操作数
        IRValue result = parseLAndExp();
        result = convert_to_i1(result);

        // 如果没有 ||，直接返回
        if (current_token().type != "OR") {
            print_non_terminal("LOrExp");
            return result;
        }

        // 分配一个栈空间用于存储短路求值的结果 (i1 类型)
        // 【修改点】使用具名寄存器（借助 label 计数器生成唯一名称），避免打乱 unnamed register (%0, %1) 的顺序
        std::string res_ptr = "%" + ir_generator.new_label("or_res");
        alloca_buffers.top() << "  " << res_ptr << " = alloca i1, align 1\n";

        // 将左操作数的值存入
        ir_generator.write_func("store i1 " + result.name + ", i1* " + res_ptr + ", align 1");

        while (current_token().type == "OR") {
            print_non_terminal("LOrExp");
            match("OR");

            // 创建基本块标签
            std::string true_label = ir_generator.new_label("or_true");   // 左侧为真，直接短路
            std::string calc_label = ir_generator.new_label("or_calc");   // 左侧为假，需要计算右侧
            std::string merge_label = ir_generator.new_label("or_merge"); // 合并

            // 根据当前结果 (result) 分支
            ir_generator.write_func("br i1 " + result.name + ", label %" + true_label + ", label %" + calc_label);

            // True Block: 短路，结果设为 1 (true)
            ir_generator.write_func("\n" + true_label + ":");
            ir_generator.write_func("store i1 1, i1* " + res_ptr + ", align 1");
            ir_generator.write_func("br label %" + merge_label);

            // Calc Block: 计算右操作数
            ir_generator.write_func("\n" + calc_label + ":");
            // 必须重置 flag，因为这是新的基本块起点
            basic_block_terminated = false;
            IRValue val2 = parseLAndExp();
            val2 = convert_to_i1(val2);
            ir_generator.write_func("store i1 " + val2.name + ", i1* " + res_ptr + ", align 1");
            ir_generator.write_func("br label %" + merge_label);

            // Merge Block: 汇合，加载结果
            ir_generator.write_func("\n" + merge_label + ":");
            basic_block_terminated = false; // merge 块是开放的
            std::string loaded_res = ir_generator.new_reg();
            ir_generator.write_func(loaded_res + " = load i1, i1* " + res_ptr + ", align 1");

            // 更新 result，作为下一次循环的左操作数
            result = IRValue {loaded_res, "i1"};
        }

        print_non_terminal("LOrExp");
        return result;
    }

    // LAndExp -> EqExp { '&&' EqExp }
    IRValue parseLAndExp() {
        // 1. 解析左操作数
        IRValue result = parseEqExp();
        result = convert_to_i1(result);

        if (current_token().type != "AND") {
            print_non_terminal("LAndExp");
            return result;
        }

        // 分配结果存储空间
        // 【修改点】使用具名寄存器
        std::string res_ptr = "%" + ir_generator.new_label("and_res");
        alloca_buffers.top() << "  " << res_ptr << " = alloca i1, align 1\n";

        // 存储左操作数
        ir_generator.write_func("store i1 " + result.name + ", i1* " + res_ptr + ", align 1");

        while (current_token().type == "AND") {
            print_non_terminal("LAndExp");
            match("AND");

            std::string false_label = ir_generator.new_label("and_false"); // 左侧为假，短路
            std::string calc_label = ir_generator.new_label("and_calc");   // 左侧为真，计算右侧
            std::string merge_label = ir_generator.new_label("and_merge"); // 合并

            // 根据结果分支: 注意 br i1 true_dest, false_dest
            // 如果 result 为真 (1)，去 calc_label；如果为假 (0)，去 false_label
            ir_generator.write_func("br i1 " + result.name + ", label %" + calc_label + ", label %" + false_label);

            // False Block: 短路，结果设为 0 (false)
            ir_generator.write_func("\n" + false_label + ":");
            ir_generator.write_func("store i1 0, i1* " + res_ptr + ", align 1");
            ir_generator.write_func("br label %" + merge_label);

            // Calc Block: 计算右操作数
            ir_generator.write_func("\n" + calc_label + ":");
            basic_block_terminated = false;
            IRValue val2 = parseEqExp();
            val2 = convert_to_i1(val2);
            ir_generator.write_func("store i1 " + val2.name + ", i1* " + res_ptr + ", align 1");
            ir_generator.write_func("br label %" + merge_label);

            // Merge Block
            ir_generator.write_func("\n" + merge_label + ":");
            basic_block_terminated = false;
            std::string loaded_res = ir_generator.new_reg();
            ir_generator.write_func(loaded_res + " = load i1, i1* " + res_ptr + ", align 1");

            result = IRValue {loaded_res, "i1"};
        }

        print_non_terminal("LAndExp");
        return result;
    }

    // EqExp -> RelExp { ('==' | '!=') RelExp }
    IRValue parseEqExp() {
        IRValue result = parseRelExp();

        while (current_token().type == "EQL" || current_token().type == "NEQ") {
            std::string op = current_token().type;
            print_non_terminal("EqExp");
            match(op);

            IRValue val2 = parseRelExp();

            // 1. 选择 LLVM 谓词
            result = ensure_i32(result);
            val2 = ensure_i32(val2);

            // 2. 生成 icmp 指令，将 i32 结果转换为 i1
            std::string predicate = (op == "EQL") ? "eq" : "ne";
            std::string result_reg = ir_generator.new_reg();
            std::string ir_line = result_reg + " = icmp " + predicate + " i32 " + result.name + ", " + val2.name;
            ir_generator.write_func(ir_line);

            // 3. 更新结果为 i1 类型
            result = IRValue {result_reg, "i1"};
        }

        print_non_terminal("EqExp");
        return result;
    }

    // RelExp -> AddExp { ('<' | '>' | '<=' | '>=') AddExp }
    IRValue parseRelExp() {
        IRValue result = parseAddExp(); // i32 值

        while (current_token().type == "LSS" || current_token().type == "GRE" ||
               current_token().type == "LEQ" || current_token().type == "GEQ") {

            std::string op = current_token().type;
            print_non_terminal("RelExp");
            match(op);

            IRValue val2 = parseAddExp(); // i32 值
            result = ensure_i32(result);
            val2 = ensure_i32(val2);
            // 1. 选择 LLVM 谓词 (icmp predicate)
            std::string predicate;
            if (op == "LSS") predicate = "slt"; // <
            else if (op == "GRE") predicate = "sgt"; // >
            else if (op == "LEQ") predicate = "sle"; // <=
            else if (op == "GEQ") predicate = "sge"; // >=

            // 2. 生成 icmp 指令，将 i32 结果转换为 i1
            std::string result_reg = ir_generator.new_reg();
            // icmp <谓词> i32 <操作数1>, <操作数2>
            std::string ir_line = result_reg + " = icmp " + predicate + " i32 " + result.name + ", " + val2.name;
            ir_generator.write_func(ir_line);

            // 3. 更新结果为 i1 类型
            result = IRValue {result_reg, "i1"};
        }

        print_non_terminal("RelExp");
        // 如果没有比较运算符，结果仍然是 i32，由上层函数转换
        return result;
    }

    // AddExp -> MulExp { ('+' | '-') MulExp }
    IRValue parseAddExp() {
        IRValue left_val = parseMulExp();
        while (current_token().type == "PLUS" || current_token().type == "MINU") {
            print_non_terminal("AddExp");
            std::string op = current_token().value;
            match(current_token().type);
            //parseMulExp();
            IRValue right_val = parseMulExp();
            if (is_const_context) {
                // 注意：栈是后进先出，所以先弹出的是 right，再是 left
                if (const_value_stack.size() >= 2) {
                    int r_val = const_value_stack.top().value; const_value_stack.pop();
                    int l_val = const_value_stack.top().value; const_value_stack.pop();
                    int res = 0;
                    if (op == "+") res = l_val + r_val;
                    else res = l_val - r_val;
                    const_value_stack.push({res, true});
                }
            }
            std::string op_code = (op == "+") ? "add nsw" : "sub nsw";
            std::string result_reg = ir_generator.new_reg();
            ir_generator.write_func(
                    result_reg + " = " + op_code + " i32 " + left_val.name + ", " + right_val.name
            );

            // 更新 left_val 为新的寄存器
            left_val = {result_reg, "i32"};
        }
        print_non_terminal("AddExp");
        return left_val;
    }

    // MulExp -> UnaryExp { ('*' | '/' | '%') UnaryExp }
    IRValue parseMulExp() {
        IRValue left_val = parseUnaryExp();
        while (current_token().type == "MULT" || current_token().type == "DIV" || current_token().type == "MOD") {
            print_non_terminal("MulExp");
            std::string op = current_token().value;
            match(current_token().type);
            //parseUnaryExp();
            IRValue right_val = parseUnaryExp();
            if (is_const_context) {
                if (const_value_stack.size() >= 2) {
                    int r_val = const_value_stack.top().value; const_value_stack.pop();
                    int l_val = const_value_stack.top().value; const_value_stack.pop();
                    int res = 0;
                    if (op == "*") res = l_val * r_val;
                    else if (op == "/") res = (r_val != 0) ? l_val / r_val : 0; // 防止除0崩溃
                    else if (op == "%") res = (r_val != 0) ? l_val % r_val : 0;
                    const_value_stack.push({res, true});
                }
            }
            std::string op_code;
            if (op == "*") op_code = "mul nsw";
            else if (op == "/") op_code = "sdiv";
            else if (op == "%") op_code = "srem";
            std::string result_reg = ir_generator.new_reg();
            ir_generator.write_func(
                    result_reg + " = " + op_code + " i32 " + left_val.name + ", " + right_val.name
            );
            left_val = {result_reg, "i32"};
        }
        print_non_terminal("MulExp");
        return left_val;
    }

    // UnaryExp -> PrimaryExp | Ident '(' [FuncRParams] ')' | UnaryOp UnaryExp
    // --- Parser::parseUnaryExp (完整修改) ---

    // UnaryExp -> PrimaryExp | Ident '(' [FuncRParams] ')' | UnaryOp UnaryExp
    IRValue parseUnaryExp() {
        const std::string& T1 = current_token().type;
        const std::string& T2 = peek(1).type;

        IRValue result_val;

        // 1. UnaryOp 开头 (+, -, !)
        if (T1 == "PLUS" || T1 == "MINU" || T1 == "NOT") {
            std::string op = current_token().value;
            match(T1);
            print_non_terminal("UnaryOp");

            IRValue operand = parseUnaryExp();
            if (is_const_context && !const_value_stack.empty()) {
                if (op == "-") {
                    int val = const_value_stack.top().value;
                    const_value_stack.pop();
                    const_value_stack.push({-val, true});
                } else if (op == "!") {
                    int val = const_value_stack.top().value;
                    const_value_stack.pop();
                    const_value_stack.push({!val, true});
                }
            }

            if (op == "-") {
                std::string result_reg = ir_generator.new_reg();
                ir_generator.write_func(
                        result_reg + " = sub i32 0, " + operand.name
                );
                result_val = {result_reg, "i32"};
            }
            else if (op == "+") {
                result_val = operand;
            }
            else if (op == "!") {
                std::string result_reg = ir_generator.new_reg();
                ir_generator.write_func(
                        result_reg + " = icmp eq i32 " + operand.name + ", 0"
                );
                std::string zext_reg = ir_generator.new_reg();
                ir_generator.write_func(
                        zext_reg + " = zext i1 " + result_reg + " to i32"
                );
                result_val = {zext_reg, "i32"};
            }
        }
            // 2. 函数调用 Ident '(' [FuncRParams] ')'
        else if (T1 == "IDENFR" && T2 == "LPARENT") {
            Token ident_tok = current_token();
            match("IDENFR");

            Symbol func_symbol;
            bool declared = find_symbol(ident_tok.value, func_symbol);
            if (!declared) { ERROR_c(ident_tok.line); }
            // 语义检查：B 错误检查省略，假设语义正确

            match("LPARENT");

            std::vector<IRValue> actual_ir_args;
            std::vector<std::string> actual_types;

            if (current_token().type != "RPARENT") {
                size_t param_index = 0;
                do {
                    // 【修改点】: 移除了原来针对 int[] 的复杂特判逻辑
                    // 直接调用 parseExp，因为 parseLVal 现在能够正确处理数组切片（返回指针）
                    IRValue arg_val = parseExp();

                    // 根据返回值的类型记录参数类型，用于后续检查
                    if (arg_val.type == "i32*") {
                        actual_types.push_back("int[]");
                    } else {
                        actual_types.push_back("int");
                    }

                    actual_ir_args.push_back(arg_val);
                    param_index++;

                } while (current_token().type == "COMMA" && (match("COMMA"), true));
            }
            int actual_param_count = actual_types.size();

            match_with_error_check("RPARENT", 'j', ident_tok.line);

            // 2. 集中进行 d 和 e 检查
            if (declared && func_symbol.param_count >= 0) {
                bool is_d_error = false;

                // --- 检查 D 错误 (个数) ---
                if (func_symbol.param_count != actual_param_count) {
                    ERROR_d(ident_tok.line);
                    is_d_error = true;
                }

                // --- 检查 E 错误 (类型) ---
                if (!is_d_error) {
                    for (size_t i = 0; i < actual_param_count; ++i) {
                        if (actual_types[i] != func_symbol.param_types[i]) {
                            ERROR_e(ident_tok.line);
                            break;
                        }
                    }
                }
            }

            // --- 2.1 LLVM IR GENERATION for FuncCall ---
            std::string ret_llvm_type = (func_symbol.type == "void") ? "void" : "i32";
            std::string result_reg = "";
            std::string call_ir = "call " + ret_llvm_type + " @" + func_symbol.name + "(";

            for (size_t i = 0; i < actual_ir_args.size(); ++i) {
                call_ir += actual_ir_args[i].type + " " + actual_ir_args[i].name;
                if (i < actual_ir_args.size() - 1) {
                    call_ir += ", ";
                }
            }
            call_ir += ")";

            if (ret_llvm_type == "void") {
                ir_generator.write_func(call_ir);
                result_val = {"", "void"};
            } else {
                result_reg = ir_generator.new_reg();
                ir_generator.write_func(result_reg + " = " + call_ir);
                result_val = {result_reg, "i32"};
            }
        }
            // 3. PrimaryExp
        else {
            result_val = parsePrimaryExp();
        }

        print_non_terminal("UnaryExp");
        return result_val;
    }
    // FuncRParams -> Exp { ',' Exp }
    FuncRParamsResult parseFuncRParams() {
        FuncRParamsResult result;

        while (true) {
            // 2. 解析 Exp 并捕获其 IRValue
            IRValue arg_ir_value = parseExp();

            // 3. 收集 IRValue 和语义类型
            result.ir_values.push_back(arg_ir_value);
            result.semantic_types.push_back("int");
            if (current_token().type != "COMMA") break;

            match("COMMA");
        }

        print_non_terminal("FuncRParams");
        // 【关键】返回实参类型列表，而不是个数
        return result;
    }
    // PrimaryExp -> '(' Exp ')' | LVal | Number (INTCON)
    IRValue parsePrimaryExp() {
        const std::string& type = current_token().type;
        IRValue exp_type ;

        if (type == "LPARENT") {
            match("LPARENT");
            exp_type = parseExp();
            match_with_error_check("RPARENT", 'j', peek(-1).line);
        } else if (type == "INTCON") {
            int val = std::stoi(current_token().value); // 获取整数值
            if (is_const_context) {
                const_value_stack.push({val, true}); // 【新增】压入栈
            }
            exp_type = {current_token().value, "i32"};
            match("INTCON");
            print_non_terminal("Number");
        } else if(type == "IDENFR") {
            exp_type = parseLVal(false); // 捕获 LVal 类型
            if (is_const_context) {
                int val = 0;
                // 尝试将返回的 IRValue.name (如 "10") 转为 int
                // parseLVal 对于 const 标量已经修改为直接返回字面量字符串
                try {
                    // 简单的检查，防止转换寄存器名 (如 %1) 导致崩溃
                    if (!exp_type.name.empty() && (isdigit(exp_type.name[0]) || exp_type.name[0] == '-' || exp_type.name[0] == '+')) {
                        val = std::stoi(exp_type.name);
                    }
                } catch (...) {
                    val = 0;
                }
                const_value_stack.push({val, true});
            }
        } else {
            return {"0", "i32"}; // 兜底返回类型
        }
        print_non_terminal("PrimaryExp");
        return exp_type;
    }


public:
    Parser(const char* outfile_path) : current_index(0) {
        output_file = fopen(outfile_path, "w");
        if (!output_file) {
            fprintf(stderr, "Failed to open parser output file.\n");
            exit(1);
        }
        g_builtin_symbols["getint"] = {"getint", "int", false, false, {}, 0, 0, 0, {}};

        // printf: void 返回, 参数可变 (param_count = -1)
        g_builtin_symbols["printf"] = {"printf", "void", false, false, {}, 0, 0, -1, {}};
    }

    ~Parser() {
        if (output_file) fclose(output_file);
    }
    std::string get_final_ir() {
        return ir_generator.get_final_ir();
    }
    void parse() {
        parseCompUnit();
        if (current_token().type != "EOF") {
            fprintf(stderr, "Parsing finished, but unexpected tokens remain starting at line %d.\n", current_token().line);
            exit(1);
        }
    }
};

// --- 第四部分：Main 函数和驱动逻辑 ---

// [省略 main 函数中的文件路径定义和 pretreatment 调用]
#include <iostream>
#include <fstream>
#include "MipsGenerator.h"

int main() {
    // 假设您的源代码文件名为 testfile.txt
//    char yuan[] = "C:\\Users\\W\\CLionProjects\\Compiler\\testfile.txt";
//    const char yuchli[] = "C:\\Users\\W\\CLionProjects\\Compiler\\preprocessing.txt";
//    const char cifa[] = "C:\\Users\\W\\CLionProjects\\Compiler\\lexer.txt";
//    const char error_path[] = "C:\\Users\\W\\CLionProjects\\Compiler\\error.txt";
//    const char parser_output_path[] = "C:\\Users\\W\\CLionProjects\\Compiler\\parser.txt";
    const char yuan[] = "testfile.txt";
    const char yuchli[] = "preprocessing.txt";
    const char cifa[] = "lexer.txt";
    const char error_path[] = "error.txt";
    const char parser_output_path[] = "parser.txt";

    pretreatment(yuan, yuchli);
    FILE* check_file = fopen(yuchli, "r");
    if (check_file == nullptr) {
        fprintf(stderr, "Error: Source file '%s' not found.\n", yuan);
        return 1;
    }
    fclose(check_file);

    // 1. 预处理 (您需要自己实现此部分，此处仅为调用)
    // pretreatment(yuan, yuchli);

    // 2. 词法分析 (填充 g_tokens)
    // 假设 yuchli 存在或直接使用 yuan
    lexical_analysis(yuchli, cifa, error_path);
    // 3. 语法分析和语义分析
    Parser parser(parser_output_path);
    parser.parse();

    if(g_error_file) fclose(g_error_file);
    sort_error_file(error_path);
    std::sort(g_symbol_output_records.begin(), g_symbol_output_records.end(),
              [](const SymbolOutputRecord& a, const SymbolOutputRecord& b) {
                  if (a.scope_id != b.scope_id) {
                      return a.scope_id < b.scope_id;
                  }
                  if (a.line_declared != b.line_declared) {
                      return a.line_declared < b.line_declared;
                  }
                  return a.insert_id < b.insert_id;
              });
    // 4. 新增：LLVM IR 生成与输出
    std::string final_ir = parser.get_final_ir();

    //const char llvm_ir_path[] = "C:\\Users\\W\\CLionProjects\\Compiler\\llvm_ir.txt";
    const char llvm_ir_path[] = "llvm_ir.txt";
    // 如果在非 Windows 环境，建议使用相对路径：const char llvm_ir_path[] = "llvm_ir.txt";

    std::ofstream llvm_ir_file(llvm_ir_path);
    if (llvm_ir_file.is_open()) {
        llvm_ir_file << final_ir;
        llvm_ir_file.close();
    } else {
        fprintf(stderr, "Error: Failed to open llvm_ir.txt for writing.\n");
        return 1;
    }
    // 输出到 symbol.txt
    //g_symbol_file = fopen("C:\\Users\\W\\CLionProjects\\Compiler\\symbol.txt", "w");
    g_symbol_file = fopen("symbol.txt", "w");
    if (g_symbol_file == nullptr) {
        fprintf(stderr, "Error: Failed to open symbol file (symbol.txt) for writing.\\n");
        // 在这里执行清理工作并安全退出
        return 1;
    }
    if (g_symbol_file) {
        for (const auto& record : g_symbol_output_records) {
            fprintf(g_symbol_file, "%d %s %s\n",
                    record.scope_id,
                    record.name.c_str(),
                    record.type_name.c_str());
        }
        fclose(g_symbol_file);
    }
    MipsGenerator generator("llvm_ir.txt", "mips.txt");
    generator.generate();
    return 0;
}