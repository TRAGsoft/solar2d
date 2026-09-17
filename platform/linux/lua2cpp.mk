
UPPER_CONFIG = $(shell echo $(config) | tr '[:lower:]' '[:upper:]')

ROOT = ..

SRCS =	shell.lua \
		$(ROOT)/resources/init.lua \
		$(ROOT)/../external/lpeg/re.lua \
		$(ROOT)/resources/json.lua \
		$(ROOT)/resources/dkjson.lua \
		$(ROOT)/../subrepos/easing/easing.lua \
		\
		$(ROOT)/../librtt/Corona/CoronaLibrary.lua \
		$(ROOT)/../librtt/Corona/CoronaPrototype.lua \
		$(ROOT)/../librtt/Corona/CoronaProvider.lua \
		$(ROOT)/../tools/CoronaBuilder/BuilderPluginDownloader.lua \
		$(ROOT)/resources/CoronaBuilderPluginCollector.lua \
		\
		$(ROOT)/../librtt/Display/Shader/kernel_composite_add_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_average_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_color_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_colorBurn_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_colorDodge_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_darken_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_difference_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_exclusion_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_glow_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_hardLight_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_hardMix_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_hue_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_lighten_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_linearLight_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_luminosity_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_multiply_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_negation_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_normalMapWith1DirLight_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_normalMapWith1PointLight_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_overlay_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_phoenix_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_pinLight_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_reflect_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_saturation_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_screen_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_softLight_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_subtract_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_vividLight_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_composite_yuv420f_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_default_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_bloom_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_blur_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_blurGaussian_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_blurHorizontal_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_blurVertical_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_brightness_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_bulge_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_chromaKey_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_color_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_colorChannelOffset_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_colorMatrix_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_colorPolynomial_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_contrast_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_crosshatch_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_crystallize_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_desaturate_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_dissolve_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_duotone_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_emboss_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_exposure_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_frostedGlass_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_grayscale_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_hue_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_invert_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_iris_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_levels_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_linearWipe_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_median_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_monotone_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_opTile_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_pixelate_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_polkaDots_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_posterize_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_radialWipe_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_saturate_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_scatter_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_sepia_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_sharpenLuminance_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_sobel_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_step_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_straighten_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_swirl_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_vignette_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_vignetteMask_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_water_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_wobble_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_woodCut_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_filter_zoomBlur_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_generator_checkerboard_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_generator_lenticularHalo_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_generator_linearGradient_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_generator_marchingAnts_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_generator_perlinNoise_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_generator_radialGradient_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_generator_random_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_generator_stripes_gl.lua \
		$(ROOT)/../librtt/Display/Shader/kernel_generator_sunbeams_gl.lua \
		$(ROOT)/../librtt/Display/Shader/shell_default_gl.lua \
		\
		$(ROOT)/../plugins/network/shared/network.lua \
		$(ROOT)/resources/ValidateSettings.lua \
		$(ROOT)/resources/valid_config_lua.lua \
		$(ROOT)/resources/valid_build_settings.lua \
		$(ROOT)/resources/linuxPackageApp.lua \
		$(ROOT)/../platform/android/create_build_properties.lua \
		$(ROOT)/../tools/CoronaBuilder/CoronaBuilder.lua
	

CPPS = $(patsubst %.lua, lua/%.cpp, $(notdir $(SRCS)))

define ruletemp
$(patsubst %.lua, lua/%.cpp, $(notdir $(1))): $(1) ../../bin/rcc.lua
	@mkdir -p lua
	@BUILD_CONFIG=$(UPPER_CONFIG) ./lua_to_native.sh $$< lua/ # $$@
endef

$(foreach src, $(SRCS), $(eval $(call ruletemp, $(src))) )


.PHONY: all

lua/CoronaLibrary-lua.cpp: lua/CoronaLibrary.cpp
	cp $< $@


# luasocket to c

PLATFORMDIR=../../bin/linux
ifeq ($(shell uname -s),Darwin)
	PLATFORMDIR = ../../bin/mac
endif

# weird luaload renames

RENAMES = lua/network_luaload.cpp lua/valid_config_lua_luaload.cpp lua/valid_build_settings_luaload.cpp lua/ValidateSettings_luaload.cpp

define ruletemp
$(1): $(subst _luaload,, $(1))
	cp $(subst _luaload,, $(1)) $(1)
endef

$(foreach src, $(RENAMES), $(eval $(call ruletemp, $(src))) )


# aggregate target

all: $(CPPS) lua/CoronaLibrary-lua.cpp $(SOCKET_DST) $(RENAMES)
	./lua_to_native.sh -m luasocket ../../external/luasocket/src/ftp.lua lua
	./lua_to_native.sh -m luasocket ../../external/luasocket/src/headers.lua lua
	./lua_to_native.sh -m luasocket ../../external/luasocket/src/http.lua lua
	./lua_to_native.sh -m luasocket ../../external/luasocket/src/ltn12.lua lua
	./lua_to_native.sh -m luasocket ../../external/luasocket/src/mbox.lua lua
	./lua_to_native.sh -m luasocket ../../external/luasocket/src/mime.lua lua
	./lua_to_native.sh -m luasocket ../../external/luasocket/src/smtp.lua lua
	./lua_to_native.sh -m luasocket ../../external/luasocket/src/socket.lua lua
	./lua_to_native.sh -m luasocket ../../external/luasocket/src/ssl.lua lua
	./lua_to_native.sh -m luasocket ../../external/luasocket/src/tp.lua lua
	./lua_to_native.sh -m luasocket ../../external/luasocket/src/url.lua lua

clean:
	mkdir -p lua
	rm -f lua/*.cpp


.DEFAULT_GOAL := all