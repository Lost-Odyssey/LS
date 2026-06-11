// Test Vec bounds check and pop reset

import std.vec

// Test 1: pop后string元素被重置
Vec(Str) v1 = {}
v1.push("hello")
print("v1.len before pop:", v1.len())
v1.pop()
print("v1.len after pop:", v1.len())
print("v1[0] after pop:", v1[0])
print("v1[0] should be empty:", v1[0].len())

// Test 2: bounds check with warning
Vec(int) v2 = {}
v2.push(10)
v2.push(20)
print("v2[0]:", v2[0])
print("v2[1]:", v2[1])
print("v2[99] (out of bounds):", v2[99])

// Test 3: negative index
print("v2[-1] (negative):", v2[-1])

print("All tests done!")