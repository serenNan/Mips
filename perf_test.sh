#!/bin/bash

echo "=== Performance Test ==="

for level in A B C; do
    if [ -d "测试程序库/$level" ]; then
        for tc in 测试程序库/$level/testcase*; do
            echo -n "$tc: "
            cp "$tc/testfile.txt" testfile.txt
            ./compiler 2>&1 > /dev/null
            
            # 统计 MIPS 指令数 (排除空行、注释、标签、伪指令)
            instr_count=$(grep -E "^\s+(li|lw|sw|add|sub|mul|div|and|or|xor|sll|srl|slt|seq|sne|sge|sle|sgt|beq|bne|j|jal|jr|move|la|syscall|mflo|mfhi)" mips.txt | wc -l)
            
            # 统计栈帧分配
            stack_alloc=$(grep -o "subu \$sp, \$sp, [0-9]*" mips.txt | awk -F', ' '{sum+=$3} END {print sum}')
            
            echo "指令数: $instr_count, 总栈分配: ${stack_alloc:-0} 字节"
        done
    fi
done
