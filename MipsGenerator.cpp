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
    if (regs[victim].dirty) {
        int offset = getStackOffset(var);
        emit("sw " + getRegName(victim) + ", " + std::to_string(offset) + "($fp) # Spill " + var);
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
            // 【修正：处理全局变量/常量 (@)】
            // 1. Load Address: r = LA @global_name
            std::string global_label = var_name.substr(1);
            emit("la " + getRegName(reg) + ", " + global_label + " # Load Global Address of " + var_name);
            // 2. Load Value: r = LW 0(r)
            emit("lw " + getRegName(reg) + ", 0(" + getRegName(reg) + ") # Load Global Value " + var_name);
            regs[reg].dirty = false;
        } else {
            // 局部变量/临时变量 (% 或其他): 从栈加载值
            int offset = getStackOffset(var_name);
            emit("lw " + getRegName(reg) + ", " + std::to_string(offset) + "($fp) # Load " + var_name);
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
            if (regs[i].dirty && !regs[i].name.empty()) {
                int offset = getStackOffset(regs[i].name);
                emit("sw " + getRegName(i) + ", " + std::to_string(offset) + "($fp) # Flush " + regs[i].name);
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
    // 复用之前的逻辑，这里为了节省篇幅简写，重点是 .data 段的生成
    // 请将上一版代码中的 parseGlobalVars 完整复制过来
    std::string line;
    while (std::getline(llvm_file, line)) {
        if (line.find("@") == 0 && line.find("global") != std::string::npos) {
            std::stringstream ss(line);
            std::string name_token;
            ss >> name_token;
            std::string name = name_token.substr(1);
            mips_file << name << ": ";

            if (line.find("zeroinitializer") != std::string::npos) {
                // 简化处理：假设数组大小在 line 里
                // 实际应解析 [N x i32]
                mips_file << ".word 0:1024 \n"; // 暴力分配，或者按实际大小
            } else {
                // 简化处理显式初始化
                // 这里需要根据你的具体实现完善，简单起见输出 .word 0
                mips_file << ".word 0\n";
            }
        }
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
            current_stack_offset = 0;

            std::string ret, name_args;
            ss >> ret >> name_args;
            std::string func_name = name_args.substr(1, name_args.find('(')-1);

            mips_file << "\n" << func_name << ":\n";
            // Prologue
            emit("sw $fp, -4($sp)");
            emit("sw $ra, -8($sp)");
            emit("move $fp, $sp");
            emit("subu $sp, $sp, 2048"); // 栈帧

            // 处理函数参数 (LLVM IR 中参数也是局部变量)
            // 你的 IR 中参数会先被 store 到栈上，所以不需要这里特殊处理
            // 这里只需要重置状态
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
            // 只需要分配栈空间，不需要生成汇编指令
            // 如果是数组，解析大小
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
            // alloca 的返回值是地址，我们可以把它视为一个存放在寄存器里的值
            // 其实就是 $fp + offset。
            // 但为了简单，LLVM 后续会用 gep 计算，这里先不处理
        }
        else if (op == "add" || op == "sub" || op == "mul" || op == "sdiv" || op == "srem") {
            // %3 = add i32 %1, %2
            std::string type, s1, s2;
            ss >> type >> s1 >> s2;
            if (s1.back() == ',') s1.pop_back();

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
            // %4 = load i32, i32* %3
            std::string val_type, ptr_type, ptr;
            ss >> val_type >> ptr_type >> ptr; // ptr_type 含有逗号
            ptr = ptr_type; // 容错处理简单的 split
            // 严谨点应该循环读到最后一个
            while(ss >> ptr);

            // 指针地址
            int r_ptr = getReg(ptr, false);
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
            // %2 = getelementptr inbounds i32, i32* %1, i32 %0
            // 简化处理：base + offset
            // 解析出 base 指针 和 最后一个 offset
            // 这部分逻辑较复杂，简化假设 offset 已经通过 mul 算好了字节数
            // 但 LLVM 中 gep 的 offset 是元素个数。

            // 假设格式: ... type* %base, ... i32 %offset
            std::string base, idx;
            // 简单的解析策略：
            std::vector<std::string> parts;
            std::string temp;
            while (ss >> temp) parts.push_back(temp);

            // parts 通常含有逗号，需要清理
            for(auto& p : parts) if(p.back()==',') p.pop_back();
            base = parts[1]; // 假设 parts[0] 是类型，parts[1] 是基址
            // 如果 parts[1] 不是 % 或 @，可能是类型，继续找
            int idx_val_pos = -1;
            for(int i=0; i<parts.size(); i++) {
                if (parts[i].find('*') != std::string::npos) {
                    base = parts[i+1]; // 指针类型后的下一个是基址
                }
            }
            idx = parts.back(); // 最后一个是偏移

            int r_base = getReg(base, false);
            int r_idx = getReg(idx, false);
            int r_dest = getReg(dest, true);

            // 计算 offset in bytes: idx * 4
            // 这里占用一个临时寄存器，或者直接利用 r_dest 计算
            emit("sll " + getRegName(r_dest) + ", " + getRegName(r_idx) + ", 2"); // dest = idx * 4
            // 如果 base 是全局变量(@a)，需要先 la 加载地址
            if (base[0] == '@') {
                int r_temp = findFreeReg();
                if (r_temp == -1) r_temp = spillReg();
                emit("la " + getRegName(r_temp) + ", " + base.substr(1));
                emit("addu " + getRegName(r_dest) + ", " + getRegName(r_dest) + ", " + getRegName(r_temp));
                regs[r_temp].busy = false; // 释放临时
            }
            else if (op == "alloca") {
                // 如果 base 是 alloca 出来的局部数组首地址
                // alloca 在 MIPS 里仅仅是栈偏移
                int offset = getStackOffset(base);
                emit("addu " + getRegName(r_dest) + ", " + getRegName(r_dest) + ", $fp");
                emit("addiu " + getRegName(r_dest) + ", " + getRegName(r_dest) + ", " + std::to_string(offset));
            }
            else {
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
        std::string type, val, ptr_type, ptr;
        ss >> type >> val >> ptr_type >> ptr; // ptr_type 含逗号
        if (val.back() == ',') val.pop_back();

        int r_val = getReg(val, false);
        int r_ptr = getReg(ptr, false);

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
        emit("lw $ra, -8($fp)");
        emit("lw $fp, -4($fp)");
        emit("addiu $sp, $sp, 2048");
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
            emit("lw $t8, " + std::to_string(offset) + "($fp)"); // 使用临时寄存器 $t8
            emit("bne $t8, $zero, " + l1.substr(1));
            emit("j " + l2.substr(1));
        }
    }
}