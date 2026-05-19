targets = [
    # keep sorted
    "autogvm",
]

la_variants = [
    # keep sorted
    "consolidate",
    "perf",
]

lv_variants = [
    # keep sorted
    "debug-defconfig",
    "defconfig",
]


def get_all_la_variants():
    return [(t, v) for t in targets for v in la_variants]

def get_all_lv_variants():
    return [(t, v) for t in targets for v in lv_variants]

def get_all_variants():
    return get_all_la_variants() + get_all_lv_variants()
