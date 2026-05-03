for d in 4 8 16 32 64 100 128 256 512; do
    ./test $d student.csv \
        --reps 10 --warmup 2 \
        --csv-out ./result/random/raw_d${d}_random.csv \
        --sorting 0 \
        >./result/random/raw_d${d}_random.out
done

for d in 4 8 16 32 64 100 128 256 512; do
    ./test $d student.csv \
        --reps 10 --warmup 2 \
        --csv-out ./result/sorted/raw_d${d}_sorted.csv \
        --sorting 1 \
        >./result/sorted/raw_d${d}_sorted.out
done

for d in 4 8 16 32 64 100 128 256 512; do
    ./test $d student.csv \
        --reps 10 --warmup 2 \
        --csv-out ./result/reversed/raw_d${d}_reversed.csv \
        --sorting -1 \
        >./result/reversed/raw_d${d}_reversed.out
done