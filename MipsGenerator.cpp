//
// Created by W on 2025/11/26.
//

#include "MipsGenerator.h"
#include <iostream>
#include <algorithm>
#include <climits>
std::string current_function_name;
MipsGenerator::MipsGenerator(const std::string& llvm_path, const std::string& mips_path) {
    llvm_file.open(llvm_path);
    mips_file.open(mips_path);
    current_stack_offset = 0;
    time_counter = 0;
    current_function_name = "";

    // 初始化寄存器状态
    for(int i=0; i<10; i++) {
        regs[i].busy = false;
        regs[i].dirty = false;
        regs[i].last_use = 0;
    }
}

MipsGenerator::~MipsGenerator() {
    if (llvm_file.is_open()) llvm_file.close();
    if (mips_file.is_open()) mips_file.close();
}

void MipsGenerator::emit(const std::string& asm_code) {
    mips_file << "    " << asm_code << "\n";
}

// 检查偏移是否在 16 位有符号立即数范围内
static bool isSmallOffset(int offset) {
    return offset >= -32768 && offset <= 32767;
}

// 生成 lw 指令，处理大偏移
void MipsGenerator::emitLoadWord(const std::string& dest_reg, int offset, const std::string& base_reg) {
    if (isSmallOffset(offset)) {
        emit("lw " + dest_reg + ", " + std::to_string(offset) + "(" + base_reg + ")");
    } else {
        // 大偏移：使用 $v1 作为临时寄存器 (不使用 $at，因为 SPIM 保留)
        emit("li $v1, " + std::to_string(offset));
        emit("addu $v1, " + base_reg + ", $v1");
        emit("lw " + dest_reg + ", 0($v1)");
    }
}

// 生成 sw 指令，处理大偏移
void MipsGenerator::emitStoreWord(const std::string& src_reg, int offset, const std::string& base_reg) {
    if (isSmallOffset(offset)) {
        emit("sw " + src_reg + ", " + std::to_string(offset) + "(" + base_reg + ")");
    } else {
        // 大偏移：使用 $v1 作为临时寄存器 (不使用 $at，因为 SPIM 保留)
        emit("li $v1, " + std::to_string(offset));
        emit("addu $v1, " + base_reg + ", $v1");
        emit("sw " + src_reg + ", 0($v1)");
    }
}

// 生成地址加载指令 (addiu)，处理大偏移
void MipsGenerator::emitLoadAddress(const std::string& dest_reg, int offset, const std::string& base_reg) {
    if (isSmallOffset(offset)) {
        emit("addiu " + dest_reg + ", " + base_reg + ", " + std::to_string(offset));
    } else {
        // 大偏移：使用 li + addu
        emit("li " + dest_reg + ", " + std::to_string(offset));
        emit("addu " + dest_reg + ", " + base_reg + ", " + dest_reg);
    }
}

bool MipsGenerator::isNumber(const std::string& s) {
    if (s.empty()) return false;
    size_t start = 0;
    if (s[0] == '-' || s[0] == '+') start = 1;
    for (size_t i = start; i < s.size(); ++i) {
        if (!isdigit(s[i])) return false;
    }
    return true;
}

std::string MipsGenerator::getRegName(int index) {
    return "$t" + std::to_string(index);
}

// 分配栈空间（如果尚未分配）
void MipsGenerator::allocStack(const std::string& var_name, int size) {
    if (stack_map.find(var_name) == stack_map.end()) {
        current_stack_offset -= size;
        stack_map[var_name] = current_stack_offset;
    }
}

int MipsGenerator::getStackOffset(const std::string& var_name) {
    if (stack_map.find(var_name) == stack_map.end()) {
        allocStack(var_name); // 兜底分配
    }
    return stack_map[var_name];
}

// --- 寄存器分配核心 ---

// 溢出策略：LRU (Least Recently Used)
int MipsGenerator::spillReg() {
    int victim = -1;
    int min_time = INT_MAX;

    // 找到 last_use 最小的寄存器
    for (int i = 0; i < 10; ++i) {
        if (regs[i].busy && regs[i].last_use < min_time) {
            min_time = regs[i].last_use;
            victim = i;
        }
    }

    if (victim == -1) {
        // 理论上不会发生，除非所有寄存器都空（逻辑错误）
        return 0;
    }

    // 执行溢出操作
    std::string var = regs[victim].name;
    // * alloca 变量不需要溢出（它的值是地址，是常量）
    if (regs[victim].dirty && !(is_alloca_var.count(var) && is_alloca_var[var])) {
        int offset = getStackOffset(var);
        emitStoreWord(getRegName(victim), offset, "$fp");
        emit("# Spill " + var);
    }

    // 清理状态
    var_in_reg.erase(var);
    regs[victim].busy = false;
    regs[victim].dirty = false;

    return victim;
}

int MipsGenerator::findFreeReg() {
    for (int i = 0; i < 10; ++i) {
        if (!regs[i].busy) return i;
    }
    return -1;
}

// 获取寄存器
// var_name: 变量名或立即数
// is_def: 是否是定义的变量（即作为赋值目标）。如果是，不需要从内存加载旧值。
// is_addr: 特殊标记，如果是 store 的地址部分，确保它在寄存器
int MipsGenerator::getReg(const std::string& var_name, bool is_def, bool is_addr) {
    time_counter++;

    // 1. 如果是数字立即数
    // 我们分配一个临时寄存器装载它，通常不记录在 var_in_reg 中，用完即扔（或者可以优化）
    // 为了简化 LRU 逻辑，这里我们也将其视为普通变量，但名字要是唯一的（防止冲突）
    if (isNumber(var_name)) {
        int reg = findFreeReg();
        if (reg == -1) reg = spillReg();

        emit("li " + getRegName(reg) + ", " + var_name);
        // 数字不占用 var_in_reg 映射，只是临时占用寄存器
        regs[reg].busy = true;
        regs[reg].name = ""; // 匿名
        regs[reg].dirty = false;
        regs[reg].last_use = time_counter;
        return reg;
    }

    // 2. 如果变量已经在寄存器中
    if (var_in_reg.count(var_name)) {
        int reg = var_in_reg[var_name];
        regs[reg].last_use = time_counter;
        if (is_def) regs[reg].dirty = true; // 如果这次是写操作，标记为脏
        return reg;
    }

    // 3. 需要分配新寄存器
    int reg = findFreeReg();
    if (reg == -1) reg = spillReg();

    // 4. 占用寄存器
    regs[reg].busy = true;
    regs[reg].name = var_name;
    regs[reg].last_use = time_counter;
    var_in_reg[var_name] = reg;

    // 5. 如果是读操作（不是定义），则从栈加载旧值
    if (!is_def) {
        if (var_name[0] == '@') {
            // 【处理全局变量/常量 (@)】
            std::string raw_label = var_name.substr(1);
            // 字符串常量以 .str 开头不需要前缀，其他全局变量需要 _ 前缀
            std::string global_label = (raw_label[0] == '.') ? raw_label : ("_" + raw_label);
            if (is_addr) {
                // 只取地址（用于 load/store 指令的指针操作数）
                emit("la " + getRegName(reg) + ", " + global_label + " # Load Global Address of " + var_name);
            } else {
                // 取值（用于普通表达式）
                emit("la " + getRegName(reg) + ", " + global_label + " # Load Global Address of " + var_name);
                emit("lw " + getRegName(reg) + ", 0(" + getRegName(reg) + ") # Load Global Value " + var_name);
            }
            regs[reg].dirty = false;
        } else if (is_alloca_var.count(var_name) && is_alloca_var[var_name]) {
            // * alloca 变量：其值是地址，计算 $fp + offset
            int offset = getStackOffset(var_name);
            emitLoadAddress(getRegName(reg), offset, "$fp");
            emit("# Address of " + var_name);
            regs[reg].dirty = false;
        } else {
            // 普通局部变量/临时变量: 从栈加载值
            int offset = getStackOffset(var_name);
            emitLoadWord(getRegName(reg), offset, "$fp");
            emit("# Load " + var_name);
            regs[reg].dirty = false;
        }
    } else {
        regs[reg].dirty = true; // 定义操作，初始就是脏的
    }
    return reg;
}

// 强制写回所有脏寄存器（在跳转、函数调用、Label前调用）
void MipsGenerator::flushRegisters() {
    for (int i = 0; i < 10; ++i) {
        if (regs[i].busy) {
            // * alloca 变量不需要写回（它的值是地址，是常量）
            if (regs[i].dirty && !regs[i].name.empty() &&
                !(is_alloca_var.count(regs[i].name) && is_alloca_var[regs[i].name])) {
                int offset = getStackOffset(regs[i].name);
                emitStoreWord(getRegName(i), offset, "$fp");
                emit("# Flush " + regs[i].name);
            }
            regs[i].busy = false;
            regs[i].dirty = false;
            regs[i].name = "";
        }
    }
    var_in_reg.clear();
}

// --- 流程控制 ---

void MipsGenerator::generate() {
    if (!llvm_file.is_open() || !mips_file.is_open()) return;

    mips_file << ".data\n";
    parseGlobalVars();

    mips_file << "\n.text\n";
    mips_file << "jal main\n";
    mips_file << "li $v0, 10\nsyscall\n";

    llvm_file.clear();
    llvm_file.seekg(0);
    parseFunctions();
}

void MipsGenerator::parseGlobalVars() {
    // 解析全局变量和常量数组
    std::string line;
    while (std::getline(llvm_file, line)) {
        // * 处理全局变量: @name = global i32 0, align 4
        // 注意：排除字符串常量（@.str 开头）
        if (line.find("@") == 0 && line.find("@.str") == std::string::npos && line.find("= global") != std::string::npos) {
            std::stringstream ss(line);
            std::string name_token;
            ss >> name_token;
            std::string name = name_token.substr(1);
            // 添加下划线前缀，避免与 MIPS 指令名（如 b, j）冲突
            mips_file << "_" << name << ": ";

            if (line.find("zeroinitializer") != std::string::npos) {
                // 数组零初始化，解析 [N x i32]
                size_t bracket_start = line.find('[');
                size_t x_pos = line.find('x');
                if (bracket_start != std::string::npos && x_pos != std::string::npos) {
                    int num = std::stoi(line.substr(bracket_start + 1, x_pos - bracket_start - 1));
                    mips_file << ".word 0:" << num << "\n";
                } else {
                    mips_file << ".word 0\n";
                }
            } else if (line.find("] [") != std::string::npos) {
                // * 带初始化列表的数组: @arr = global [N x i32] [i32 10, i32 25, ...], align 4
                size_t init_start = line.find("] [");
                if (init_start != std::string::npos) {
                    init_start += 3; // 跳过 "] ["
                    size_t init_end = line.find("]", init_start);
                    std::string init_list = line.substr(init_start, init_end - init_start);

                    // 解析 i32 N, i32 M, ...
                    std::vector<int> values;
                    std::stringstream init_ss(init_list);
                    std::string token;
                    while (std::getline(init_ss, token, ',')) {
                        size_t i32_pos = token.find("i32");
                        if (i32_pos != std::string::npos) {
                            std::string num_str = token.substr(i32_pos + 3);
                            // 去除空格
                            num_str.erase(0, num_str.find_first_not_of(" \t"));
                            num_str.erase(num_str.find_last_not_of(" \t") + 1);
                            if (!num_str.empty()) {
                                values.push_back(std::stoi(num_str));
                            }
                        }
                    }

                    // 输出数组值 (SPIM 用逗号分隔)
                    mips_file << ".word ";
                    for (size_t i = 0; i < values.size(); ++i) {
                        if (i > 0) mips_file << ", ";
                        mips_file << values[i];
                    }
                    mips_file << "\n";
                }
            } else {
                // * 简单标量初始化: @c = global i32 3, align 4
                // 解析 "global i32 <value>" 中的 value
                size_t i32_pos = line.find("i32 ");
                if (i32_pos != std::string::npos) {
                    std::string rest = line.substr(i32_pos + 4);
                    // 提取数字直到逗号或空格
                    size_t end = rest.find_first_of(", ");
                    std::string value_str = rest.substr(0, end);
                    // 去除前后空格
                    value_str.erase(0, value_str.find_first_not_of(" \t"));
                    value_str.erase(value_str.find_last_not_of(" \t") + 1);
                    mips_file << ".word " << value_str << "\n";
                } else {
                    mips_file << ".word 0\n";
                }
            }
        }
        // * 处理常量数组: @ia1_9 = constant [5 x i32] [i32 1, i32 2, ...], align 4
        // ! 注意：不能以 @.str 开头（那是字符串常量）
        else if (line.find("@") == 0 && line.find("@.str") == std::string::npos && line.find("constant") != std::string::npos && line.find("x i32]") != std::string::npos) {
            std::stringstream ss(line);
            std::string name_token;
            ss >> name_token;
            std::string name = name_token.substr(1);
            // 添加下划线前缀，避免与 MIPS 指令名冲突
            mips_file << "_" << name << ": .word ";

            // 提取数组初始化值 [i32 1, i32 2, i32 3, ...]
            size_t init_start = line.find("] [");
            if (init_start != std::string::npos) {
                init_start += 3; // 跳过 "] ["
                size_t init_end = line.find("]", init_start);
                std::string init_list = line.substr(init_start, init_end - init_start);

                // 解析 i32 N, i32 M, ...
                std::vector<int> values;
                std::stringstream init_ss(init_list);
                std::string token;
                while (std::getline(init_ss, token, ',')) {
                    size_t i32_pos = token.find("i32");
                    if (i32_pos != std::string::npos) {
                        std::string num_str = token.substr(i32_pos + 3);
                        // 去除空格
                        num_str.erase(0, num_str.find_first_not_of(" \t"));
                        num_str.erase(num_str.find_last_not_of(" \t") + 1);
                        if (!num_str.empty()) {
                            values.push_back(std::stoi(num_str));
                        }
                    }
                }

                // 输出数组值 (SPIM 用逗号分隔)
                for (size_t i = 0; i < values.size(); ++i) {
                    if (i > 0) mips_file << ", ";
                    mips_file << values[i];
                }
                mips_file << "\n";
            }
        }
        // * 处理字符串常量: @.str = private unnamed_addr constant [6 x i8] c"crsb\0A\00", align 1
        else if (line.find("@.str") != std::string::npos && line.find("constant") != std::string::npos) {
            // 字符串常量处理：@.str = private unnamed_addr constant [6 x i8] c"crsb\0A\00", align 1
            std::stringstream ss(line);
            std::string name;
            ss >> name;
            mips_file << name.substr(1) << ": .asciiz ";

            // 找到字符串部分：c"..."
            size_t c_start = line.find("c\"");
            size_t c_end = line.rfind('\"');

            if (c_start != std::string::npos && c_end != std::string::npos && c_end > c_start) {
                // 提取 c"..." 中的内容，跳过 c"
                std::string raw_content = line.substr(c_start + 2, c_end - (c_start + 2));

                std::string processed_content = "\"";
                for (size_t i = 0; i < raw_content.length(); ++i) {
                    if (raw_content[i] == '\\' && i + 2 < raw_content.length()) {
                        // 处理转义序列，如 \0A, \00
                        std::string hex = raw_content.substr(i + 1, 2);
                        if (hex == "0A") {
                            processed_content += "\\n"; // LLVM \0A -> MIPS \n
                            i += 2;
                        } else if (hex == "09") {
                            processed_content += "\\t"; // LLVM \09 -> MIPS \t
                            i += 2;
                        } else if (hex == "00") {
                            // \00 是字符串结束符，.asciiz 会自动添加
                            i += 2;
                            continue;
                        } else {
                            // 如果是其他转义，保持原样 (不推荐，但作为兜底)
                            processed_content += raw_content[i];
                        }
                    } else {
                        processed_content += raw_content[i];
                    }
                }
                processed_content += "\"";
                mips_file << processed_content << "\n";
            }
        }
    }
}

void MipsGenerator::parseFunctions() {
    std::string line;
    bool in_function = false;

    while (std::getline(llvm_file, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "define") {
            in_function = true;
            flushRegisters(); // 安全起见
            stack_map.clear();
            is_alloca_var.clear(); // 清空 alloca 标记
            current_stack_offset = 0;

            // 从完整的 line 解析函数定义
            // 格式: define i32 @func2(i32 %arg1, i32 %arg2) {
            size_t at_pos = line.find('@');
            size_t paren_start = line.find('(');
            size_t paren_end = line.find(')');

            std::string func_name = line.substr(at_pos + 1, paren_start - at_pos - 1);

            mips_file << "\n" << func_name << ":\n";
            // Prologue
            // 栈布局: $sp(原) -> [$fp saved], [$ra saved], [locals...]
            // 先减 $sp 为保存区腾出空间
            emit("subu $sp, $sp, 8");     // 为 $fp 和 $ra 预留空间
            emit("sw $fp, 4($sp)");       // 保存旧 $fp 在 $sp+4
            emit("sw $ra, 0($sp)");       // 保存旧 $ra 在 $sp+0
            emit("addiu $fp, $sp, 8");    // $fp 指向旧栈顶，局部变量从 $fp-12 开始
            emit("subu $sp, $sp, 2048");  // 栈帧 (小型栈帧)
            current_stack_offset = -12;   // 局部变量从 $fp-12 开始（跳过保存区）

            // 处理函数参数: 解析参数列表，将 $a0-$a3 保存到栈上
            if (paren_start != std::string::npos && paren_end != std::string::npos) {
                std::string args_str = line.substr(paren_start + 1, paren_end - paren_start - 1);
                if (!args_str.empty()) {
                    std::vector<std::string> arg_names;
                    std::stringstream args_ss(args_str);
                    std::string arg_part;
                    while (std::getline(args_ss, arg_part, ',')) {
                        // 去除前后空白
                        size_t start = arg_part.find_first_not_of(" \t");
                        if (start == std::string::npos) continue;
                        arg_part = arg_part.substr(start);
                        // 格式: "i32 %arg1" 或 "i32* %arg1"
                        std::stringstream part_ss(arg_part);
                        std::string type, name;
                        part_ss >> type >> name;
                        if (!name.empty()) {
                            arg_names.push_back(name);
                        }
                    }
                    // 将参数从 $a0-$a3 保存到栈
                    const char* arg_regs[] = {"$a0", "$a1", "$a2", "$a3"};
                    for (size_t i = 0; i < arg_names.size() && i < 4; ++i) {
                        allocStack(arg_names[i]);
                        int offset = getStackOffset(arg_names[i]);
                        emitStoreWord(std::string(arg_regs[i]), offset, "$fp");
                        emit("# Save arg " + arg_names[i]);
                    }
                }
            }
        }
        else if (token == "}") {
            in_function = false;
        }
        else if (in_function) {
            processInstruction(line);
        }
    }
}

void MipsGenerator::processInstruction(const std::string& line) {
    std::stringstream ss(line);
    std::string token;
    ss >> token;

    // 1. Label (基本块入口)
    // 必须 Flush，因为不知道从哪跳过来的
    if (token.back() == ':') {
        flushRegisters();
        std::string label_name = token.substr(0, token.length() - 1);
        if (label_name == "entry" || label_name == "0") {
            return;
        }
        std::string unique_label = current_function_name + "_" + label_name;
        mips_file << token << "\n";
        return;
    }

    // 2. 赋值指令
    if (token.find('%') == 0) {
        std::string dest = token;
        std::string assign, op;
        ss >> assign >> op; // = opcode

        if (op == "alloca") {
            // %1 = alloca i32
            // 分配栈空间，alloca 的结果是该空间的地址
            int size = 4;
            std::string type;
            ss >> type;
            if (type.find('[') != std::string::npos) {
                // [10 x i32] -> 提取 10
                size_t x = type.find('x');
                int num = std::stoi(type.substr(1, x-1));
                size = num * 4;
            }
            allocStack(dest, size);
            // * 标记这个变量是 alloca 出来的，其值是地址
            is_alloca_var[dest] = true;
        }
        else if (op == "add" || op == "sub" || op == "mul" || op == "sdiv" || op == "srem") {
            // %2 = add nsw i32 %0, %1
            // 可能有 nsw/nuw 修饰符，需要跳过
            std::string type, s1, s2;
            ss >> type;
            // 跳过 nsw, nuw 等修饰符
            while (type == "nsw" || type == "nuw") {
                ss >> type;
            }
            ss >> s1 >> s2;
            if (!s1.empty() && s1.back() == ',') s1.pop_back();

            int r1 = getReg(s1, false);
            int r2 = getReg(s2, false);
            int rd = getReg(dest, true); // dest 是定义

            std::string mips_op = "addu";
            if (op == "sub") mips_op = "subu";
            else if (op == "mul") mips_op = "mul";
            else if (op == "sdiv") mips_op = "div";
            else if (op == "srem") mips_op = "div"; // 取余也要 div

            if (op == "sdiv") {
                emit("div " + getRegName(r1) + ", " + getRegName(r2));
                emit("mflo " + getRegName(rd));
            } else if (op == "srem") {
                emit("div " + getRegName(r1) + ", " + getRegName(r2));
                emit("mfhi " + getRegName(rd));
            } else {
                emit(mips_op + " " + getRegName(rd) + ", " + getRegName(r1) + ", " + getRegName(r2));
            }
        }
        else if (op == "load") {
            // %0 = load i32, i32* %i_2_addr, align 4
            // 解析格式: load <val_type>, <ptr_type> <ptr>, align <n>
            std::string val_type, ptr_type, ptr;
            ss >> val_type >> ptr_type >> ptr;
            // 去掉 ptr 末尾的逗号（如果有）
            if (!ptr.empty() && ptr.back() == ',') ptr.pop_back();

            // 指针地址 - 使用 is_addr=true，只取地址不取值
            int r_ptr = getReg(ptr, false, true);
            int r_dest = getReg(dest, true);
            emit("lw " + getRegName(r_dest) + ", 0(" + getRegName(r_ptr) + ")");
        }
        else if (op == "icmp") {
            // %3 = icmp eq i32 %1, %2
            std::string cond, type, s1, s2;
            ss >> cond >> type >> s1 >> s2;
            if (s1.back() == ',') s1.pop_back();

            int r1 = getReg(s1, false);
            int r2 = getReg(s2, false);
            int rd = getReg(dest, true);

            if (cond == "eq") emit("seq " + getRegName(rd) + ", " + getRegName(r1) + ", " + getRegName(r2));
            else if (cond == "ne") emit("sne " + getRegName(rd) + ", " + getRegName(r1) + ", " + getRegName(r2));
            else if (cond == "sgt") emit("sgt " + getRegName(rd) + ", " + getRegName(r1) + ", " + getRegName(r2));
            else if (cond == "sge") emit("sge " + getRegName(rd) + ", " + getRegName(r1) + ", " + getRegName(r2));
            else if (cond == "slt") emit("slt " + getRegName(rd) + ", " + getRegName(r1) + ", " + getRegName(r2));
            else if (cond == "sle") emit("sle " + getRegName(rd) + ", " + getRegName(r1) + ", " + getRegName(r2));
        }
        else if (op == "getelementptr") {
            // %4 = getelementptr inbounds [2 x i8], [2 x i8]* @.str0, i32 0, i32 0
            // 解析：跳过 inbounds，找到 base 指针和最后一个 offset
            std::vector<std::string> parts;
            std::string temp;
            while (ss >> temp) parts.push_back(temp);

            // 清理逗号
            for(auto& p : parts) if(!p.empty() && p.back()==',') p.pop_back();

            // 找 base（第一个 * 后面的变量）和 idx（最后一个值）
            std::string base, idx;
            for(size_t i = 0; i < parts.size(); i++) {
                if (parts[i].find('*') != std::string::npos && i + 1 < parts.size()) {
                    base = parts[i + 1];
                    break;
                }
            }
            idx = parts.back();

            int r_dest = getReg(dest, true);

            // 如果 base 是全局变量（@ 开头），直接 la 加载地址
            if (base[0] == '@') {
                std::string raw_label = base.substr(1);
                // 字符串常量以 .str 开头不需要前缀，其他全局变量需要 _ 前缀
                std::string label = (raw_label[0] == '.') ? raw_label : ("_" + raw_label);
                // 对于字符串常量或数组，offset 通常是 0，直接 la 即可
                if (isNumber(idx) && std::stoi(idx) == 0) {
                    emit("la " + getRegName(r_dest) + ", " + label);
                } else {
                    // 非零 offset：计算 base + idx * element_size
                    int r_idx = getReg(idx, false);
                    emit("sll " + getRegName(r_dest) + ", " + getRegName(r_idx) + ", 2");
                    int r_temp = findFreeReg();
                    if (r_temp == -1) r_temp = spillReg();
                    emit("la " + getRegName(r_temp) + ", " + label);
                    emit("addu " + getRegName(r_dest) + ", " + getRegName(r_dest) + ", " + getRegName(r_temp));
                    regs[r_temp].busy = false;
                }
            }
            else {
                // 局部变量指针
                int r_base = getReg(base, false);
                int r_idx = getReg(idx, false);
                emit("sll " + getRegName(r_dest) + ", " + getRegName(r_idx) + ", 2");
                emit("addu " + getRegName(r_dest) + ", " + getRegName(r_dest) + ", " + getRegName(r_base));
            }
        }
        else if (op == "zext") {
            // %2 = zext i1 %1 to i32
            // MIPS 中不做区分，直接 move
            std::string type1, s1, to, type2;
            ss >> type1 >> s1 >> to >> type2;
            int r_src = getReg(s1, false);
            int r_dst = getReg(dest, true);
            emit("move " + getRegName(r_dst) + ", " + getRegName(r_src));
        }
        else if (op == "call") {
            // %3 = call i32 @func(...)
            // Flush registers before call!
            flushRegisters();

            // 解析函数名和参数
            // 简化：假设没有参数，或参数已经在 parseFuncDef 中处理了
            std::string ret_type, func_name;
            ss >> ret_type >> func_name;
            size_t p = func_name.find('(');
            std::string real_name = func_name.substr(1, p-1);
            std::string args_str = line.substr(line.find('(')+1);
            args_str.pop_back(); // 去掉 )

            // 逗号分割
            std::stringstream arg_ss(args_str);
            std::string segment;
            int arg_idx = 0;
            while(std::getline(arg_ss, segment, ',')) {
                // segment: "i32 %10"
                std::stringstream seg_ss(segment);
                std::string type, val;
                seg_ss >> type >> val;

                // 加载参数到寄存器
                int r = getReg(val, false);
                if (arg_idx < 4) {
                    emit("move $a" + std::to_string(arg_idx) + ", " + getRegName(r));
                } else {
                    // 超过4个参数压栈，这里略
                }
                arg_idx++;
            }

            if (real_name == "getint") {
                emit("li $v0, 5");
                emit("syscall");
                // 结果在 $v0，存入 dest
                int rd = getReg(dest, true);
                emit("move " + getRegName(rd) + ", $v0");
            }
            else if (real_name == "putint") {
                emit("li $v0, 1");
                emit("syscall");
            }
            else if (real_name == "putstr") {
                emit("li $v0, 4");
                emit("syscall");
            }
            else if (real_name == "putch") {
                emit("li $v0, 11");
                emit("syscall");
            }
            else {
                emit("jal " + real_name);
                if (ret_type != "void") {
                    int rd = getReg(dest, true);
                    emit("move " + getRegName(rd) + ", $v0");
                }
            }
        }
    }
        // 3. Store 指令
    else if (token == "store") {
        // store i1 %1, i1* %and_res4, align 1
        std::string type, val, ptr_type, ptr;
        ss >> type >> val >> ptr_type >> ptr;
        // 去掉逗号
        if (!val.empty() && val.back() == ',') val.pop_back();
        if (!ptr.empty() && ptr.back() == ',') ptr.pop_back();

        int r_val = getReg(val, false);
        int r_ptr = getReg(ptr, false, true);  // is_addr=true，只取地址不取值

        emit("sw " + getRegName(r_val) + ", 0(" + getRegName(r_ptr) + ")");
    }
        // 4. Ret 指令
    else if (token == "ret") {
        flushRegisters(); // 返回前必须写回，虽然栈帧即将销毁，但如果是 void 函数可能有副作用
        std::string type, val;
        ss >> type;
        if (type != "void") {
            ss >> val;
            // 加载返回值到 $v0
            // 这里不能用 getReg，因为我们马上要退出了，直接 lw 或者 li
            int r_val = getReg(val, false);
            emit("move $v0, " + getRegName(r_val));
        }

        // Epilogue
        // 栈布局: $fp 指向旧栈顶，$ra 在 $fp-8，$fp 在 $fp-4
        emit("subu $sp, $fp, 8");   // 恢复 $sp 到保存区
        emit("lw $ra, 0($sp)");     // 恢复 $ra
        emit("lw $fp, 4($sp)");     // 恢复 $fp
        emit("addiu $sp, $sp, 8");  // 释放保存区
        emit("jr $ra");
    }
        // 5. Br 指令
    else if (token == "br") {
        flushRegisters(); // 跳转前必须写回
        std::string label_or_cond;
        ss >> label_or_cond;

        if (label_or_cond == "label") {
            std::string label;
            ss >> label; // %label1
            emit("j " + label.substr(1));
        } else {
            // br i1 %cond, label %true, label %false
            // 你的 IR 中 type 是 i1
            std::string cond = label_or_cond; // i1
            std::string val_name, l1_kw, l1, l2_kw, l2;
            ss >> val_name; // %cond,
            if(val_name.back()==',') val_name.pop_back();

            ss >> l1_kw >> l1 >> l2_kw >> l2; // label %true, label %false
            if(l1.back()==',') l1.pop_back();

            // 加载条件变量。由于我们已经 flush 了，这里只能手动 lw
            int offset = getStackOffset(val_name);
            emitLoadWord("$t8", offset, "$fp"); // 使用临时寄存器 $t8
            emit("bne $t8, $zero, " + l1.substr(1));
            emit("j " + l2.substr(1));
        }
    }
    // 6. Void call 指令 (没有返回值的函数调用)
    else if (token == "call") {
        // call void @putint(i32 %3)
        flushRegisters();

        std::string ret_type, func_name;
        ss >> ret_type >> func_name;
        size_t p = func_name.find('(');
        std::string real_name = func_name.substr(1, p-1);
        std::string args_str = line.substr(line.find('(')+1);
        if (!args_str.empty() && args_str.back() == ')') args_str.pop_back();

        // 解析参数
        std::stringstream arg_ss(args_str);
        std::string segment;
        int arg_idx = 0;
        while(std::getline(arg_ss, segment, ',')) {
            std::stringstream seg_ss(segment);
            std::string type, val;
            seg_ss >> type >> val;
            if (val.empty()) continue;

            int r = getReg(val, false);
            if (arg_idx < 4) {
                emit("move $a" + std::to_string(arg_idx) + ", " + getRegName(r));
            }
            arg_idx++;
        }

        if (real_name == "putint") {
            emit("li $v0, 1");
            emit("syscall");
        }
        else if (real_name == "putstr") {
            emit("li $v0, 4");
            emit("syscall");
        }
        else if (real_name == "putch") {
            emit("li $v0, 11");
            emit("syscall");
        }
        else {
            emit("jal " + real_name);
        }
    }
}