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
# define HUE2MQTT_BUILD_CMDS
#     $(MAKE) CXX="$(TARGET_CXX)" LD="$(TARGET_LD)" PKGCONFIG="$(HOST_DIR)/bin/pkg-config" -C $(@D)
# endef

# define HUE2MQTT_INSTALL_TARGET_CMDS
#     $(INSTALL) -D -m 0755 $(@D)/hue2mqtt  $(TARGET_DIR)/usr/bin
# endef

# $(eval $(generic-package))
$(eval $(meson-package))