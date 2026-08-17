load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load("//build/kernel/kleaf:kernel.bzl", "ddk_module")

def define_modules(target, variant):
    kernel_build_variant = "{}_{}".format(target, variant)
    include_base = "../../../{}".format(native.package_name())
    qps615_rule = "{}_tc956x_pcie_eth".format(kernel_build_variant)
    r8125_rule = "{}_r8125".format(kernel_build_variant)

    mod_list = []

    base_kernel = select({
        "//build/kernel/kleaf:socrepo_true": "//soc-repo:{}_base_kernel".format(kernel_build_variant),
        "//build/kernel/kleaf:socrepo_false": "//msm-kernel:{}".format(kernel_build_variant),
    })

    # Define the conditional list of dependencies
    header_deps = select({
        "//build/kernel/kleaf:socrepo_true": ["//soc-repo:all_headers"],
        "//build/kernel/kleaf:socrepo_false": ["//msm-kernel:all_headers"],
        "//conditions:default": [],
    })

    # TC956X/QPS615 PF Driver Module
    ddk_module(
        name = qps615_rule,
        out = "tc956x_pcie_eth.ko",
        srcs = [
            "drivers/qps615/src/dwxgmac2_core.c",
            "drivers/qps615/src/dwxgmac2_descs.c",
            "drivers/qps615/src/dwxgmac2_dma.c",
            "drivers/qps615/src/hwif.c",
            "drivers/qps615/src/mmc_core.c",
            "drivers/qps615/src/tc956x_pci.c",
            "drivers/qps615/src/tc956x_pcie_logstat.c",
            "drivers/qps615/src/tc956x_pma.c",
            "drivers/qps615/src/tc956x_qcom.c",
            "drivers/qps615/src/tc956x_xpcs.c",
            "drivers/qps615/src/tc956xmac_ethtool.c",
            "drivers/qps615/src/tc956xmac_hwtstamp.c",
            "drivers/qps615/src/tc956xmac_main.c",
            "drivers/qps615/src/tc956xmac_mdio.c",
            "drivers/qps615/src/tc956xmac_ptp.c",
            "drivers/qps615/src/tc956xmac_tc.c",
            "drivers/qps615/src/tc956x_msigen.c",
            "drivers/qps615/src/tc956x_pf_mbx_wrapper.c",
            "drivers/qps615/src/tc956x_pf_mbx.c",
            "drivers/qps615/src/tc956x_pf_rsc_mng.c",
        ],
        kernel_build = base_kernel,
        deps = [":qps615_headers"] + header_deps,
        copts = [
            "-DTC956X",
            "-DCONFIG_TC956X_PLATFORM_SUPPORT",
            "-DTC956X_SRIOV_PF",
            "-DFIRMWARE_NAME=\\\"qps615_fw.bin\\\"",
        ],
    )

    mod_list.append(":{}".format(qps615_rule))

    ddk_module(
        name = r8125_rule,
        out = "r8125.ko",
        srcs = [
            "drivers/r8125/src/r8125_firmware.c",
            "drivers/r8125/src/r8125_n.c",
            "drivers/r8125/src/r8125_rss.c",
            "drivers/r8125/src/rtl_eeprom.c",
            "drivers/r8125/src/rtltool.c",
            "drivers/r8125/src/r8125_fiber.c",
        ],
        kernel_build = base_kernel,
        deps = [":r8125_headers"] + header_deps,
        copts = [
            "-Wno-error",
            "-DENABLE_USE_FIRMWARE_FILE",
            "-DCONFIG_ASPM",
            "-DENABLE_S5WOL",
            "-DENABLE_EEE",
            "-DENABLE_TX_NO_CLOSE",
            "-DCONFIG_R8125_NAPI",
            "-DCONFIG_R8125_VLAN",
            "-DENABLE_MULTIPLE_TX_QUEUE",
            "-DENABLE_RSS_SUPPORT",
        ],
    )

    # Add to list and Copy to dist
    mod_list.append(":{}".format(r8125_rule))

    copy_to_dist_dir(
        name = "{}_modules_dist".format(kernel_build_variant),
        data = mod_list,
        dist_dir = "out/target/product/{}/dlkm/lib/modules/".format(target),
        flat = True,
        wipe_dist_dir = False,
        allow_duplicate_filenames = False,
        mode_overrides = {"**/*": "644"},
        log = "info",
    )
