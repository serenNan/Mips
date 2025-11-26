#!/bin/bash

test_case() {
    local dir=$1
    echo "=== Testing $dir ==="
    cp "$dir/testfile.txt" testfile.txt
    ./compiler 2>&1 > /dev/null

    if [ -f "$dir/in.txt" ]; then
        result=$(cat "$dir/in.txt" | timeout 10 spim -lstack 10000000 -file mips.txt 2>&1 | tail -n +6)
    else
        result=$(echo "" | timeout 10 spim -lstack 10000000 -file mips.txt 2>&1 | tail -n +6)
    fi

    expected=$(cat "$dir/ans.txt")

    if [ "$result" = "$expected" ]; then
        echo "PASSED"
        return 0
    else
        echo "FAILED"
        echo "--- Expected (first 10 lines):"
        echo "$expected" | head -10
        echo "--- Got (first 10 lines):"
        echo "$result" | head -10
        return 1
    fi
}

passed=0
failed=0

for level in A B C; do
    if [ -d "测试程序库/$level" ]; then
        for tc in 测试程序库/$level/testcase*; do
            if test_case "$tc"; then
                ((passed++))
            else
                ((failed++))
            fi
            echo ""
        done
    fi
done

echo "=============================="
echo "Results: $passed passed, $failed failed"
