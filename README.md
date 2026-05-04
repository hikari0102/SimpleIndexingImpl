# SimpleIndexingImpl
Code and Data for CSE321 Assignment1 (2026 Spring)

# Compile
On linux,
```bash
g++ -std=c++17 -O2 -Wall -Wextra test.cpp -o test
```

This command will generate the test binary.

Then, run
```bash
./run.sh
```

It will read the students.csv at the project top folder and writes the results into result folder.

Note that this code used some features on C++17 such as auto [a, b] = func();

So, lowering the C++ std version will cause compilation error or other misbehaviour.

