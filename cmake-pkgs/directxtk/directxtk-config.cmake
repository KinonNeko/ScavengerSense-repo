if(NOT TARGET Microsoft::DirectXTK)
  add_library(Microsoft::DirectXTK INTERFACE IMPORTED)
  set_target_properties(Microsoft::DirectXTK PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../DirectXTK/Inc")
endif()
set(directxtk_FOUND TRUE)
