load(":data_eth.bzl", "define_data_eth_modules")

def define_autogvm():
    define_data_eth_modules(
        target = "autogvm",
        modules = [
            "emac_ctrl_fe_virtio",
            "stmmac_thin_core",
            "emac_thin",
            "emac_thin_gy",
        ],
        config_options = [
            "CONFIG_EMAC_CTRL_FE",
            "CONFIG_EMAC_SHIM",
            "CONFIG_EMAC_SHIM_GY",
        ]
    )
