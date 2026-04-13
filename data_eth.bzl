load(":target_variants.bzl", "get_all_variants")
load("//build/kernel/kleaf:kernel.bzl", "ddk_module")
load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load(":data_eth_modules.bzl", "data_eth_modules")

def _get_config_choices(config_srcs, options):
    choices = []
    for option in config_srcs:
        choices.extend(config_srcs[option].get(option in options, []))
    return choices

def _get_module_srcs(module, options):
    srcs = module.srcs + _get_config_choices(module.config_srcs, options)
    return native.glob(
        ["{}/{}".format(module.path, src) for src in srcs] + ["include/*.h"]
    )

def _get_module_deps(module, options, formatter):
    deps = module.deps + _get_config_choices(module.config_deps, options)
    print("module_deps = ", deps)
    return [formatter(dep) for dep in deps]

def _get_build_options(modules, config_options):
    all_options = {option: True for option in config_options}
    print("all_options = ", all_options)
    all_options = all_options | {module.config_opt: True for module in modules if module.config_opt}
    return all_options

def _get_module_build_options(module, config_options):
    all_options = {option: True for option in config_options}
    all_options = all_options | {module.config_opt: True}
    return all_options

def define_target_variant_modules(target, variant, modules, config_options = []):
    """
    Generates the ddk_module for each of our kernel modules.
    """
    print("target= ", target)
    print("variant= ", variant)
    print("modules= ", modules)
    print("config_options= ", config_options)

    tv = "{}_{}".format(target, variant)

    kernel_build = "//soc-repo:{}_base_kernel".format(tv)
    module_build = "//vendor/qcom/opensource/data-eth:{}".format(tv)
    modules = [data_eth_modules.get(module_name) for module_name in modules]
    print("kernel_build= ", kernel_build)
    options = _get_build_options(modules, config_options)
    formatter = lambda s : s.replace("%b", kernel_build)
    formatter2 = lambda s : s.replace("%b", module_build)

    all_modules = []
    for module in modules:
        print("module = ", module)
        rule_name = "{}_{}".format(tv, module.name)
        module_srcs = _get_module_srcs(module, options)
        module_opt = _get_module_build_options(module, config_options)
        ddk_module(
            name = rule_name,
            kernel_build = kernel_build,
            srcs = module_srcs,
            out = "{}.ko".format(module.name),
            deps = ["//common:all_headers",
                    "//soc-repo:all_headers",
                   ] + _get_module_deps(module, module_opt, formatter2),
            includes = ["include"],
            local_defines = module_opt.keys(),
            visibility = ["//visibility:public"],
        )

        all_modules.append(rule_name)

    # Use copy_to_dist_dir instead of pkg_install.
    # This rule accepts the --dist_dir argument passed by the build wrapper.
    copy_to_dist_dir(
        name = "{}_data-eth_dist".format(tv),
        data = all_modules,
        flat = True,
        visibility = ["//visibility:private"],
    )

def define_data_eth_modules(target, modules, config_options = []):
    for (t, v) in get_all_variants():
        if t == target:
            define_target_variant_modules(t, v, modules, config_options)
