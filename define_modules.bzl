load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load("//msm-kernel:target_variants.bzl", "get_all_variants")
load("//build/kernel/kleaf:kernel.bzl", "ddk_module")

def define_modules(target, variant):
    kernel_build_variant = "{}_{}".format(target, variant)
    ioss_copts = []
    emac_ioss_copts = ["-Wno-error"]

    #The below will take care of the defconfig
    include_defconfig = ":{}_defconfig".format(variant)

    if target == "sdxkova.prpl":
     ioss_copts =  ["-DFEATURE_PRPLWRT"]
     emac_ioss_copts = ["-DFEATURE_PRPLWRT","-Wno-error"]

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
        copts = ioss_copts,
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
        copts = emac_ioss_copts,
    )
    mod_list.append("{}-defconfig_emac_ioss".format(kernel_build_variant))

    ddk_module(
        name = "{}-defconfig_r8125".format(kernel_build_variant),
        out = "r8125.ko",
        srcs = [
            "drivers/rtl8125/src/r8125_firmware.c",
            "drivers/rtl8125/src/r8125_lib.c",
            "drivers/rtl8125/src/r8125_n.c",
            "drivers/rtl8125/src/r8125_rss.c",
            "drivers/rtl8125/src/rtl_eeprom.c",
            "drivers/rtl8125/src/rtltool.c",
        ],
        kernel_build = "//msm-kernel:{}-defconfig".format(kernel_build_variant),
        deps = [
            ":rtl8125_headers",
            "//msm-kernel:all_headers",
        ],
        copts = [
            "-Wno-error",
            "-DENABLE_USE_FIRMWARE_FILE",
            "-DCONFIG_ASPM",
            "-DENABLE_S5WOL",
            "-DENABLE_EEE",
            "-DENABLE_TX_NO_CLOSE",
            "-DCONFIG_R8125_NAPI",
            "-DCONFIG_R8125_VLAN",
            "-DENABLE_DOUBLE_VLAN",
            "-DENABLE_MULTIPLE_TX_QUEUE",
            "-DENABLE_RSS_SUPPORT",
            "-DENABLE_LIB_SUPPORT",
        ],
    )
    mod_list.append("{}-defconfig_r8125".format(kernel_build_variant))

    ddk_module(
        name = "{}-defconfig_r8125_ioss".format(kernel_build_variant),
        out = "r8125_ioss.ko",
        srcs = [
            "drivers/r8125_ioss/r8125_ioss.c",
        ],
        kernel_build = "//msm-kernel:{}-defconfig".format(kernel_build_variant),
        deps = [
            ":rtl8125_headers",
            ":ioss_headers",
            "//msm-kernel:all_headers",
            "//build_dir/target-aarch64_cortex-a53_musl/linux-sdx85/dataipa-1.0:include_headers",
            ":{}-defconfig_r8125".format(kernel_build_variant),
            ":{}-defconfig_ioss".format(kernel_build_variant),
        ],
    )
    mod_list.append("{}-defconfig_r8125_ioss".format(kernel_build_variant))

    ddk_module(
        name = "{}-defconfig_r8152".format(kernel_build_variant),
        out = "r8152_rtl.ko",
        srcs = [
            "drivers/r8152/compatibility.h",
            "drivers/r8152/r8152_rtl.c",
        ],
        kernel_build = "//msm-kernel:{}-defconfig".format(kernel_build_variant),
        deps = [
            "//msm-kernel:all_headers",
        ],
        copts = [
            "-Wno-error",
        ],
    )
    mod_list.append("{}-defconfig_r8152".format(kernel_build_variant))

    ddk_module(
        name = "{}-defconfig_aqc_ioss".format(kernel_build_variant),
        out = "aqc_ioss.ko",
        srcs = [
            "drivers/aqc_ioss/aqc_ioss.c",
            "drivers/aqc_ioss/aqc_ioss.h",
            "drivers/aqc_ioss/aqc_regs.c",
            "drivers/aqc_ioss/aqc_regs.h",
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
    mod_list.append("{}-defconfig_aqc_ioss".format(kernel_build_variant))

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
