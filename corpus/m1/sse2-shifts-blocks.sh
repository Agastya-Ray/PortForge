blocks=()
for opcode in 72f0 73f0 72d0 73d0 72e0; do
    for count in 0 1 31 32 63 64 255; do
        printf -v immediate '%02x' "$count"
        blocks+=("660f${opcode}${immediate}")
    done
done
