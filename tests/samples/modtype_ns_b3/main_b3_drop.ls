import mod_b3_drop

def main() -> int {
    mod_b3_drop.create_and_drop(3)
    int count = mod_b3_drop.get_drop_count()
    @print(f"drops={count}\n")

    Resource r = Resource.make("final", 99)
    int id = r.get_id()
    @print(f"id={id}\n")

    return 0
}
