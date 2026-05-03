# SimpleIndexingImpl
Code and Data for CSE321 Assignment1 (2026 Spring)

# Compile
On linux,
<pre>
g++ -std=c++17 -fsanitize=address -O2 -Wall -Wextra test.cpp -o test
</pre>
This command will generate the test binary.

Then, run
<pre>
./run.sh
</pre>

It will read the students.csv at the project top folder and writes the results into result folder.

Note that this code used some features on C++17 such as auto [a, b] = func();

So, lowering the C++ std version will cause compilation error or other misbehaviour.

