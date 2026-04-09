module main
import math
import constants

fn main() -> int {
    int result = math.add(1, 2)
    f64 p = math.pi()
    print(p)
    print(constants.MAGIC)
    
    array(int, 5) ary = [1, 2, 3, 4, 5]
    print(ary)
    
    return result
}
