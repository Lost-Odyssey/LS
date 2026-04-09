// Test: verify if struct with string field gets cleaned up
struct Inner {
    string data;
}

fn test() {
    Inner i
    i.data = "test".upper()
    print(i.data)
}
// 在这个函数退出时, i.data 应该被释放吗?