load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load("//msm-kernel:target_variants.bzl", "get_all_variants")
load("//build/kernel/kleaf:kernel.bzl", "ddk_module")

def define_modules(target, variant):
    kernel_build_variant = "{}_{}".format(target, variant)

    #The below will take care of the defconfig
    include_defconfig = ":{}_defconfig".format(variant)

    mod_list = []

    ddk_module(
        name = "{}-defconfig_ioss".format(kernel_build_variant),
        out = "ioss.ko",
        srcs = [
            "drivers/ioss/ioss_main.c",
            "drivers/ioss/ioss_bus.c",
            "drivers/ioss/ioss_pci.c",
            "drivers/ioss/ioss_net.c",
            "drivers/ioss/ioss_of.c",
            "drivers/ioss/ioss_ipa.c",
            "drivers/ioss/ioss_ipa_hal.c",
            "drivers/ioss/ioss_mem.c",
            "drivers/ioss/ioss_utils.c",
            "drivers/ioss/ioss_platform.c",
            "drivers/ioss/ioss_qos.c",
            "drivers/ioss/ioss_sysfs.c",
        ],
        kernel_build = "//msm-kernel:{}-defconfig".format(kernel_build_variant),
        deps = [
            ":ioss_headers",
            "//msm-kernel:all_headers",
            "//build_dir/target-aarch64_cortex-a53_musl/linux-sdx85/dataipa-1.0:include_headers",
            "//build_dir/target-aarch64_cortex-a53_musl/linux-sdx85/dataipa-1.0:{}-defconfig_ipam".format(kernel_build_variant),
        ],
    )
    mod_list.append("{}-defconfig_ioss".format(kernel_build_variant))


    ddk_module(
        name = "{}-defconfig_emac_ioss".format(kernel_build_variant),
        out = "iemac_ioss.ko",
        srcs = [
            "drivers/emac_ioss/emac_ioss.c",
            "drivers/emac_ioss/emac_ipa_intf.c",
            "drivers/emac_ioss/emac_ipa_intf.h",
        ],
        kernel_build = "//msm-kernel:{}-defconfig".format(kernel_build_variant),
        deps = [
            ":ioss_headers",
            "//msm-kernel:all_headers",
            "//build_dir/target-aarch64_cortex-a53_musl/linux-sdx85/dataipa-1.0:include_headers",
            ":{}-defconfig_ioss".format(kernel_build_variant),
        ],
        copts = [
            "-Wno-error",
        ],
    )
    mod_list.append("{}-defconfig_emac_ioss".format(kernel_build_variant))

    copy_to_dist_dir(
        name = "{}-defconfig_dataeth_dist".format(kernel_build_variant),
        data = mod_list,
        dist_dir = "out/target/product/{}/dlkm/lib/modules/".format(target),
        flat = True,
        wipe_dist_dir = False,
        allow_duplicate_filenames = False,
        mode_overrides = {"**/*": "644"},
        log = "info",
    )
