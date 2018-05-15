TWEAK_NAME = skia
TOOL_NAME = skiad
BUNDLE_NAME = skiapref

skia_FILES = skia.cpp config.cpp posix.cpp netcore.cpp libc++/shared_mutex.cpp
skia_FRAMEWORKS = CoreFoundation CFNetwork JavaScriptCore
skia_LIBRARIES = substrate
skia_INSTALL_PATH = /Library/MobileSubstrate/DynamicLibraries

skiad_FILES = skiad.mm
skiad_PRIVATE_FRAMEWORKS = AppSupport
skiad_INSTALL_PATH = /usr/libexec

skiapref_FILES = skiapref.mm config.cpp
skiapref_RESOURCE_DIRS = res
skiapref_FRAMEWORKS = UIKit CoreGraphics CFNetwork JavaScriptCore
skiapref_PRIVATE_FRAMEWORKS = AppSupport Preferences
skiapref_LIBRARIES = substrate
skiapref_INSTALL_PATH = /Library/PreferenceBundles

export TARGET = iphone:clang
export ARCHS = armv7 armv7s arm64
export TARGET_IPHONEOS_DEPLOYMENT_VERSION = 7.0
export ADDITIONAL_CFLAGS = -fvisibility=hidden -isystem include
export ADDITIONAL_CCFLAGS = -std=c++1y
export ADDITIONAL_OBJCFLAGS = -fobjc-arc

include $(THEOS)/makefiles/common.mk
include $(THEOS_MAKE_PATH)/tweak.mk
include $(THEOS_MAKE_PATH)/tool.mk
include $(THEOS_MAKE_PATH)/bundle.mk

internal-stage::
	$(ECHO_NOTHING)dir="$(THEOS_STAGING_DIR)/DEBIAN" && mkdir -p "$$dir" && cp scripts/postinst scripts/prerm "$$dir/"$(ECHO_END)
	$(ECHO_NOTHING)dir="$(THEOS_STAGING_DIR)/Library/LaunchDaemons" && mkdir -p "$$dir" && cp $(TOOL_NAME).plist "$$dir/me.qusic.$(TOOL_NAME).plist"$(ECHO_END)
	$(ECHO_NOTHING)dir="$(THEOS_STAGING_DIR)/Library/PreferenceLoader/Preferences" && mkdir -p "$$dir" && cp $(BUNDLE_NAME).plist "$$dir/$(TWEAK_NAME).plist"$(ECHO_END)
