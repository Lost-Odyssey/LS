// env_test.ls — Tests for the env stdlib module.

import env

fn main() -> int {
    // Test 1: env.has — PATH should always be set
    bool has_path = env.has("PATH")
    if !has_path {
        print("FAIL: PATH should exist")
        return 1
    }
    print("PASS: env.has")

    // Test 2: env.get — PATH returns Some
    Option(string) p = env.get("PATH")
    match p {
        None     => { print("FAIL: env.get PATH returned None"); return 1 }
        Some(v)  => { print("PASS: env.get") }
    }

    // Test 3: env.get on missing var returns None
    Option(string) missing = env.get("__LS_NO_SUCH_VAR_12345__")
    match missing {
        None    => { print("PASS: env.get missing") }
        Some(v) => { print("FAIL: env.get missing should be None"); return 1 }
    }

    // Test 4: env.get_or with default
    string val = env.get_or("__LS_NO_SUCH_VAR_12345__", "default_value")
    if val == "default_value" {
        print("PASS: env.get_or default")
    } else {
        print("FAIL: env.get_or default wrong value")
        return 1
    }

    // Test 5: env.set + env.get roundtrip
    env.set("__LS_TEST_VAR__", "hello_ls")
    string got = env.get_or("__LS_TEST_VAR__", "")
    if got == "hello_ls" {
        print("PASS: env.set/get roundtrip")
    } else {
        print("FAIL: env.set/get roundtrip")
        return 1
    }

    // Test 6: env.has after set
    if env.has("__LS_TEST_VAR__") {
        print("PASS: env.has after set")
    } else {
        print("FAIL: env.has after set")
        return 1
    }

    // Test 7: env.delete
    env.delete("__LS_TEST_VAR__")
    bool still_there = env.has("__LS_TEST_VAR__")
    if !still_there {
        print("PASS: env.delete")
    } else {
        print("FAIL: env.delete — var still present")
        return 1
    }

    // Test 8: env.all — returns non-empty map containing PATH
    env.set("__LS_ALL_TEST__", "all_works")
    map(string, string) all = env.all()
    if all.contains_key("__LS_ALL_TEST__") {
        print("PASS: env.all contains set var")
    } else {
        print("FAIL: env.all missing expected var")
        return 1
    }
    env.delete("__LS_ALL_TEST__")

    print("All env tests passed")
    return 0
}
