// MipsGenerator.h
#ifndef COMPILER_MIPSGENERATOR_H
#define COMPILER_MIPSGENERATOR_H

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <list>

struct RegInfo {
    std::string name; // 当前存放的变量名 (例如 "%1", "%a_addr")
    bool busy;        // 是否被占用
    bool dirty;       // 是否被修改过 (与内存不一致)
    int last_use;     // 最后一次使用的时间戳 (用于 LRU 置换)
};

class MipsGenerator {
private:
    std::ifstream llvm_file;
    std::ofstream mips_file;

    // 栈管理
    std::map<std::string, int> stack_map;
    std::map<std::string, bool> is_alloca_var; // 标记哪些变量是 alloca 出来的（值是地址）
    int current_stack_offset;

    // 寄存器管理 ($t0 - $t9, 对应索引 0 - 9)
    RegInfo regs[10];
    std::map<std::string, int> var_in_reg; // 变量 -> 寄存器索引
    int time_counter; // 模拟时间，用于 LRU

    // 辅助函数
    void parseGlobalVars();
    void parseFunctions();
    void processInstruction(const std::string& line);

    // 栈操作
    void allocStack(const std::string& var_name, int size = 4);
    int getStackOffset(const std::string& var_name);

    // 寄存器分配核心逻辑
    int getReg(const std::string& var_name, bool is_def = false, bool is_addr = false);
    int findFreeReg();
    int spillReg(); // 溢出最久未使用的寄存器
    void flushRegisters(); // 清空所有寄存器（写回脏数据）
    std::string getRegName(int index);

    // 工具
    bool isNumber(const std::string& s);
    void emit(const std::string& asm_code);

    // * 优化相关
    std::map<std::string, int> var_use_count; // 变量使用次数统计
    void preAnalyzeFunction(const std::vector<std::string>& instructions);
    bool isSmallImmediate(int val); // 检查是否可以用立即数指令

    // 大偏移处理辅助函数
    void emitLoadWord(const std::string& dest_reg, int offset, const std::string& base_reg);
    void emitStoreWord(const std::string& src_reg, int offset, const std::string& base_reg);
    void emitLoadAddress(const std::string& dest_reg, int offset, const std::string& base_reg);

public:
    MipsGenerator(const std::string& llvm_path, const std::string& mips_path);
    ~MipsGenerator();
    void generate();
};

#endif //COMPILER_MIPSGENERATOR_H