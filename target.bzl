load(":data_eth.bzl", "define_data_eth_modules")

def define_autogvm():
    define_data_eth_modules(
        target = "autogvm",
        modules = [
            "emac_ctrl_fe_virtio",
            "emac_thin",
        ],
        config_options = [
            "CONFIG_EMAC_CTRL_FE",
            "CONFIG_EMAC_SHIM",
        ]
    )
