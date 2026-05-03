// string append / += / + optimization end-to-end test

// 1. append string
string s = "hello"
s.append(" world")
print(s)

// 2. append char literal
s.append('!')
print(s)

// 3. += string
string a = "foo"
a += "bar"
print(a)

// 4. += char
a += '!'
print(a)

// 5. a = a + b optimization (in-place append path)
string x = "left"
string y = "right"
x = x + y
print(x)

// 6. normal + (different variables, creates new string)
string p = "aa"
string q = "bb"
string r = p + q
print(r)

// 7. char type variable
char c = 'Z'
string t = "hello"
t.append(c)
print(t)

// 8. chain appends
string chain = "a"
chain.append("b")
chain.append("c")
chain.append('d')
print(chain)

// 9. append to static literal string (cap==0 → must malloc)
string lit = "start"
lit.append("_end")
print(lit)
