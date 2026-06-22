load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load("//build/kernel/kleaf:kernel.bzl", "ddk_module")

def define_tc956x_modules(target, variant):
    kernel_build_variant = "{}_{}".format(target, variant)
    rule_base = "{}_tc956x_pcie_eth".format(kernel_build_variant)

    base_kernel = select({
        "//build/qcom_build_extensions:qtisocrepo_true": "//soc-repo:{}_base_kernel".format(kernel_build_variant),
        "//build/qcom_build_extensions:qtisocrepo_false": "//msm-kernel:{}".format(kernel_build_variant),
    })

    header_deps = select({
        "//build/qcom_build_extensions:qtisocrepo_true": ["//soc-repo:all_headers"],
        "//build/qcom_build_extensions:qtisocrepo_false": ["//msm-kernel:all_headers"],
    })

    ddk_module(
        name = rule_base,
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
        deps = [
            ":qps615_headers",
        ] + header_deps,
        copts = [
            "-DTC956X",
            "-DCONFIG_TC956X_PLATFORM_SUPPORT",
            "-DTC956X_SRIOV_PF",
            "-DFIRMWARE_NAME=\\\"qps615_fw.bin\\\"",
        ],
        visibility = ["//visibility:public"],
    )

    copy_to_dist_dir(
        name = "{}_tc956x_modules_dist".format(kernel_build_variant),
        data = [":{}".format(rule_base)],
        dist_dir = "out/target/product/{}/dlkm/lib/modules/".format(target),
        flat = True,
        wipe_dist_dir = False,
        allow_duplicate_filenames = False,
        mode_overrides = {"**/*": "644"},
        log = "info",
    )
