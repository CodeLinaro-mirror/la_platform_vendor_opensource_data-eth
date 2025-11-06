load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load(":target_variants.bzl", "get_all_la_variants")
load("//build/kernel/kleaf:kernel.bzl", "ddk_module")

def define_modules(target, variant):
    kernel_build_variant = "{}_{}".format(target, variant)
    include_base = "../../../{}".format(native.package_name())
    rule_base = "{}_tc956x_pcie_eth".format(kernel_build_variant)

    #The below will take care of the defconfig
    #include_defconfig = ":{}_defconfig".format(variant)

    mod_list = [":{}".format(rule_base)]

    base_kernel = select({
        "//build/kernel/kleaf:socrepo_true": "//soc-repo:{}_base_kernel".format(kernel_build_variant),
        "//build/kernel/kleaf:socrepo_false": "//msm-kernel:{}".format(kernel_build_variant),
    })

    # Define the conditional list of dependencies
    header_deps = select({
        "//build/kernel/kleaf:socrepo_true": ["//soc-repo:all_headers"],
        "//build/kernel/kleaf:socrepo_false": ["//msm-kernel:all_headers"],
        "//conditions:default": [], # Always good practice to have a default condition
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
	#defconfig = include_defconfig,
	deps = [
	    ":qps615_headers",
	] + header_deps,
    copts = [
        "-DTC956X",
        "-DCONFIG_TC956X_PLATFORM_SUPPORT",
        "-DTC956X_SRIOV_PF",
        "-DFIRMWARE_NAME=\\\"qps615_fw.bin\\\"",
    ],
    )

    copy_to_dist_dir(
       name = "{}_dist".format(rule_base),
       data = mod_list,
       dist_dir = "../out/target/product/{}/dlkm/lib/modules/".format(target),
       flat = True,
       wipe_dist_dir = False,
       allow_duplicate_filenames = False,
       mode_overrides = {"**/*": "644"},
       log = "info",
    )
    #pkg_files(
    #    name = "{}_modules_dist_files".format(kernel_build_variant),
    #    srcs = ["{}_qps615".format(kernel_build_variant)],
    #    strip_prefix = strip_prefix.files_only(),
    #    visibility = ["//visibility:private"],
    #)
    #pkg_install(
    #    name = "{}_modules_dist".format(kernel_build_variant),
    #    srcs = [":{}_modules_dist_files".format(kernel_build_variant)],
    #    destdir = "out/target/product/canoe/dlkm/lib/modules/",
    #)
