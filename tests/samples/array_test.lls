// Fixed-size array test

// Global array
array(int, 4) global_arr = [100, 200, 300, 400]
int MAGIC = 42

def sum_array(array(int, 3) data) -> int {
    int s = 0
    for x in data {
        s = s + x
    }
    s
}

def main() -> int {
    // Local array declaration + initialization
    array(int, 3) nums = [10, 20, 30]
    @print(nums[0])           // 10
    @print(nums[1])           // 20
    @print(nums[2])           // 30
    @print(nums.length)       // 3

    // Indexed assignment
    nums[1] = 99
    @print(nums[1])           // 99

    // for-in iteration + function parameter (by value)
    int total = sum_array(nums)
    @print(total)             // 139 (10+99+30)

    // print whole array
    @print(nums)              // [10, 99, 30]

    // Global variable access
    @print(global_arr[0])     // 100
    @print(global_arr.length) // 4
    @print(MAGIC)             // 42

    return 0
}
