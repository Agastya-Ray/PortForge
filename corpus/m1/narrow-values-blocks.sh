blocks=(0fb6c1 0fb6c5 0fb7c1 0fbec1 0fbec5 0fbfc1 480fbec1 480fbfc1 4863c1 480fb601 480fbf01 486301 410fb6c0 400fb6c4)
for register in c0 c4 01; do
    for condition in {0..15}; do
        printf -v opcode '%02x' "$((0x90 + condition))"
        blocks+=("0f${opcode}${register}")
    done
done
blocks+=(410f94c0)
