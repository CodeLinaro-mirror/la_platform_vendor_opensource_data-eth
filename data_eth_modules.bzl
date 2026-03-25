# Paths for data-eth modules
EMAC_SHIM_PATH = "drivers/emac_shim"
EMAC_CTRL_FE_PATH = "drivers/emac_ctrl_fe"

# This dictionary holds all the data-eth modules
data_eth_modules = {}

def register_data_eth_modules(name, path = None, config_opt = None, srcs = [], config_srcs = {}, deps = [], config_deps = {}):
    """
    Register modules
    Args:
        name: Name of the module (which will be used to generate the name of the .ko file)
        path: Path in which the source files can be found
        config_opt: Config name used in Kconfig
        srcs: source files and local headers
        config_srcs: source files and local headers that depend on a config define being enabled.
        deps: a list of dependent targets
        config_deps: a list of dependent targets that depend on a config define being enabled.
    """
    processed_config_srcs = {}
    processed_config_deps = {}

    for config_src_name in config_srcs:
        config_src = config_srcs[config_src_name]

        if type(config_src) == "list":
            processed_config_srcs[config_src_name] = {True: config_src}
        else:
            processed_config_srcs[config_src_name] = config_src

    for config_deps_name in config_deps:
        config_dep = config_deps[config_deps_name]

        if type(config_dep) == "list":
            processed_config_deps[config_deps_name] = {True: config_dep}
        else:
            processed_config_deps[config_deps_name] = config_dep

    module = struct(
        name = name,
        path = path,
        srcs = srcs,
        config_srcs = processed_config_srcs,
        config_opt = config_opt,
        deps = deps,
        config_deps = processed_config_deps,
    )
    data_eth_modules[name] = module

# --- Data ETH Modules ---

register_data_eth_modules(
    name = "emac_thin",
    path = EMAC_SHIM_PATH,
    config_opt = "CONFIG_EMAC_SHIM",
    srcs = [
        "dwmac4_descs.c",
        "dwmac4_thin.c",
        "dwmac-qcom-ethqos-thin.c",
        "hwif_thin.c",
        "stmmac_ethtool_thin.c",
        "stmmac_platform_thin.c",
        "stmmac_thin.c",
    ],
    deps = [
        "%b_emac_ctrl_fe_virtio",
        ":emac_shim_headers",
	":include_headers",
    ],
)

register_data_eth_modules(
    name = "emac_ctrl_fe_virtio",
    path = EMAC_CTRL_FE_PATH,
    config_opt = "CONFIG_EMAC_CTRL_FE",
    srcs = [
        "emac_ctrl_fe_virtio.c",
    ],
    deps = [
        ":emac_ctrl_fe_headers",
        ":include_headers",
    ],
)
