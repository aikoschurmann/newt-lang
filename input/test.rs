arr: i64[] = {1, 2, 3, 4, 5, 6};
size: i64 = 6;

fn test(arr: i64[]*, size: i64) -> i64 {
    res: i64 = 0;
    for(i: i64 = 0; i < size; i++) {
        res += (*arr)[i];
    }
    return res;
}

fn main() {
    result: i64 = test(&arr, size);
}