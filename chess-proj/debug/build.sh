python chesskiller.py

cpp_json="../outcome/chess_killer/checkpoint_cpp_test.json"

if [ -f "$cpp_json" ]; then
    rm "$cpp_json"
fi

g++ --std=c++17 ../chesskiller.cpp -o ../chesskiller
../chesskiller

echo "done"