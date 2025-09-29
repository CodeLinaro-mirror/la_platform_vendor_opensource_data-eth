load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load("//build/kernel/kleaf:kernel.bzl", "ddk_module")

def define_modules(target, variant):
    kernel_build_variant = "{}_{}".format(target, variant)
    include_base = "../../../{}".format(native.package_name())

    #The below will take care of the defconfig
    #include_defconfig = ":{}_defconfig".format(variant)

    ddk_module(
        name = "{}_qps615".format(kernel_build_variant),
        out = "tc956x_pcie_eth.ko",
        srcs = [
            "drivers/qps615/src/common.h",
            "drivers/qps615/src/descs.h",
            "drivers/qps615/src/dwxgmac2_core.c",
            "drivers/qps615/src/dwxgmac2_descs.c",
            "drivers/qps615/src/dwxgmac2_dma.c",
            "drivers/qps615/src/dwxgmac2.h",
            "drivers/qps615/src/hwif.c",
            "drivers/qps615/src/hwif.h",
            "drivers/qps615/src/mmc_core.c",
            "drivers/qps615/src/mmc.h",
            "drivers/qps615/src/tc956xmac_config.h",
            "drivers/qps615/src/tc956xmac_ethtool.c",
            "drivers/qps615/src/tc956xmac.h",
            "drivers/qps615/src/tc956xmac_hwtstamp.c",
            "drivers/qps615/src/tc956xmac_inc.h",
            "drivers/qps615/src/tc956xmac_ioctl.h",
            "drivers/qps615/src/tc956xmac_main.c",
            "drivers/qps615/src/tc956xmac_mdio.c",
            "drivers/qps615/src/tc956xmac_pcs.h",
            "drivers/qps615/src/tc956xmac_ptp.c",
            "drivers/qps615/src/tc956xmac_ptp.h",
            "drivers/qps615/src/tc956xmac_tc.c",
            "drivers/qps615/src/tc956x_msigen.c",
            "drivers/qps615/src/tc956x_pci.c",
            "drivers/qps615/src/tc956x_pcie_logstat.c",
            "drivers/qps615/src/tc956x_pcie_logstat.h",
            "drivers/qps615/src/tc956x_pf_mbx.c",
            "drivers/qps615/src/tc956x_pf_mbx.h",
            "drivers/qps615/src/tc956x_pf_mbx_wrapper.c",
            "drivers/qps615/src/tc956x_pf_rsc_mng.c",
            "drivers/qps615/src/tc956x_pf_rsc_mng.h",
            "drivers/qps615/src/tc956x_pma.c",
            "drivers/qps615/src/tc956x_pma.h",
            "drivers/qps615/src/tc956x_qcom.c",
            "drivers/qps615/src/tc956x_xpcs.c",
            "drivers/qps615/src/tc956x_xpcs.h",
        ],
        kernel_build = "//msm-kernel:{}".format(kernel_build_variant),
        deps = [
            ":qps615_headers",
            "//msm-kernel:all_headers",
        ],
        copts = [
            "-DTC956X",
            "-DCONFIG_TC956X_PLATFORM_SUPPORT",
            "-DTC956X_SRIOV_PF",
        ],
    )

    copy_to_dist_dir(
        name = "{}_modules_dist".format(kernel_build_variant),
        data = [
            "{}_qps615".format(kernel_build_variant),
        ],
        dist_dir = "out/target/product/{}/dlkm/lib/modules/".format(target),
        flat = True,
        wipe_dist_dir = False,
        allow_duplicate_filenames = False,
        mode_overrides = {"**/*": "644"},
        log = "info",
    )

