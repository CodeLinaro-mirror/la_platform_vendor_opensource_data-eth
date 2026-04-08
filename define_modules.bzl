load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load("//msm-kernel:target_variants.bzl", "get_all_variants")
load("//build/kernel/kleaf:kernel.bzl", "ddk_module")

def define_modules(target, variant):
    kernel_build_variant = "{}_{}".format(target, variant)

    #The below will take care of the defconfig
    include_defconfig = ":{}_defconfig".format(variant)

    mod_list = []

    if target == "kera-le":
        ddk_module(
            name = "{}_defconfig_tc956x".format(kernel_build_variant),
            out = "tc956x_pcie_eth.ko",
            srcs = [
                "drivers/qps615/src/dwxgmac2_core.c",
                "drivers/qps615/src/dwxgmac2_descs.c",
                "drivers/qps615/src/dwxgmac2_dma.c",
                "drivers/qps615/src/hwif.c",
                "drivers/qps615/src/mmc_core.c",
                "drivers/qps615/src/tc956xmac_ethtool.c",
                "drivers/qps615/src/tc956xmac_hwtstamp.c",
                "drivers/qps615/src/tc956xmac_main.c",
                "drivers/qps615/src/tc956xmac_mdio.c",
                "drivers/qps615/src/tc956xmac_ptp.c",
                "drivers/qps615/src/tc956xmac_tc.c",
                "drivers/qps615/src/tc956x_msigen.c",
                "drivers/qps615/src/tc956x_pci.c",
                "drivers/qps615/src/tc956x_pcie_logstat.c",
                "drivers/qps615/src/tc956x_pf_mbx.c",
                "drivers/qps615/src/tc956x_pf_mbx_wrapper.c",
                "drivers/qps615/src/tc956x_pf_rsc_mng.c",
                "drivers/qps615/src/tc956x_pma.c",
                "drivers/qps615/src/tc956x_qcom.c",
                "drivers/qps615/src/tc956x_xpcs.c",
            ],
            kernel_build = "//msm-kernel:{}-defconfig".format(kernel_build_variant),
            deps = [
                ":tc956x_headers",
                "//msm-kernel:all_headers",
            ],
            copts = [
                "-DTC956X",
                "-DCONFIG_TC956X_PLATFORM_SUPPORT",
                "-DTC956X_SRIOV_PF",
            ],
        )
        mod_list.append("{}_defconfig_tc956x".format(kernel_build_variant))
    else:
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
                "//msm-kernel:all_headers_arm",
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
                "//msm-kernel:all_headers_arm",
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
