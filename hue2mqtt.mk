################################################################################
#
# hue2mqtt package
#
################################################################################

HUE2MQTT_VERSION = 1.0
HUE2MQTT_SITE = ./package/hue2mqtt
HUE2MQTT_SITE_METHOD = local# Other methods like git,wget,scp,file etc. are also available.
HUE2MQTT_DEPENDENCIES = host-pkgconf dbus
HUE2MQTT_INSTALL_STAGING = YES

$(eval $(meson-package))