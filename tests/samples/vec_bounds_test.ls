// Test vec bounds check and pop reset

// Test 1: pop后string元素被重置
vec(string) v1
v1.push("hello")
print("v1.length before pop:", v1.length)
v1.pop()
print("v1.length after pop:", v1.length)
print("v1[0] after pop:", v1[0])
print("v1[0] should be empty:", v1[0].length)

// Test 2: bounds check with warning
vec(int) v2
v2.push(10)
v2.push(20)
print("v2[0]:", v2[0])
print("v2[1]:", v2[1])
print("v2[99] (out of bounds):", v2[99])

// Test 3: negative index
print("v2[-1] (negative):", v2[-1])

print("All tests done!")