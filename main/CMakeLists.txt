set(LIANG_ASSETS "${CMAKE_CURRENT_LIST_DIR}/../assets")

set(LIANG_EMBED)
foreach(stage RANGE 0 5)
    list(APPEND LIANG_EMBED "${LIANG_ASSETS}/images/liang/stage_${stage}.rgb565")
    list(APPEND LIANG_EMBED "${LIANG_ASSETS}/music/liang/stage${stage}_peak.pcm")
    list(APPEND LIANG_EMBED "${LIANG_ASSETS}/music/liang/stage${stage}_idle.pcm")
endforeach()

# 基线硬件测试 demo（demo_*.c、ui_pixel*.c）保留在本目录供参考与主机测试，
# 但不编入滑动变祖器固件。
idf_component_register(
    SRCS "main.c"
         "liang_app.c"
         "liang_assets.c"
         "liang_audio.c"
         "liang_clock.c"
         "liang_logic.c"
         "${LIANG_ASSETS}/fonts/liang_font_title_18.c"
         "${LIANG_ASSETS}/fonts/liang_font_num_18.c"
         "${LIANG_ASSETS}/fonts/liang_font_name_28.c"
         "${LIANG_ASSETS}/fonts/liang_font_stamp_34.c"
         "${LIANG_ASSETS}/fonts/liang_font_small_12.c"
    INCLUDE_DIRS "."
    EMBED_FILES ${LIANG_EMBED}
    REQUIRES bsp esp_event esp_netif esp_timer esp_wifi nvs_flash lwip
)

# 估算时间的下限：编译时刻（UTC epoch），在未联网且 NVS 无存档时使用。
string(TIMESTAMP LIANG_BUILD_EPOCH "%s" UTC)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LIANG_BUILD_EPOCH=${LIANG_BUILD_EPOCH}LL)
