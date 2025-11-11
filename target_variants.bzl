targets = [
   # keep sorted
   "canoe",
]

la_variants = [
   # keep sorted
   "consolidate",
   "perf",
]


def get_all_la_variants():
    return [(t, v) for t in targets for v in la_variants]
