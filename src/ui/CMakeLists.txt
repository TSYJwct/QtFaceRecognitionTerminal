aux_source_directory(. _UI_FILES)
aux_source_directory(touch _LIB_TOUCH_FILES)
add_library(ui STATIC ${_UI_FILES} ${_LIB_TOUCH_FILES})

target_include_directories(
  ui
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/ui>
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/ui/touch>)

target_link_libraries(
  ui
  PUBLIC app
  PUBLIC Qt5::Charts)
