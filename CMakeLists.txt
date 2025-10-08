# ====================================================================
# Vix.cpp — CLI Module
# ====================================================================
cmake_minimum_required(VERSION 3.20)
project(vix_cli VERSION 0.1.0 LANGUAGES CXX)

# --- Language / Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Sources
# Note: CONFIGURE_DEPENDS pour regénérer quand de nouveaux .cpp arrivent
file(GLOB_RECURSE CLI_SOURCES
     CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(vix_cli ${CLI_SOURCES})
add_executable(Vix::cli ALIAS vix_cli)

# --- Includes (du module CLI uniquement)
target_include_directories(vix_cli PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# --- Warnings cross-platform (auto-détection)
include(CheckCXXCompilerFlag)
function(vix_add_flag_if_supported tgt flag)
  string(REGEX REPLACE "[^A-Za-z0-9]" "_" flag_var "${flag}")
  set(test_var "HAVE_${flag_var}")
  check_cxx_compiler_flag("${flag}" ${test_var})
  if(${test_var})
    target_compile_options(${tgt} PRIVATE "${flag}")
  endif()
endfunction()

if (MSVC)
  vix_add_flag_if_supported(vix_cli /W4)
  vix_add_flag_if_supported(vix_cli /permissive-)
else()
  vix_add_flag_if_supported(vix_cli -Wall)
  vix_add_flag_if_supported(vix_cli -Wextra)
  vix_add_flag_if_supported(vix_cli -Wshadow)
  vix_add_flag_if_supported(vix_cli -Wconversion)
endif()

# --- Dépendance vers Vix::utils
# 1) si la cible existe (build dans le super-projet vix/) → lier directement
# 2) sinon, tenter un find_package(vix_utils CONFIG) (build isolé)
if (TARGET Vix::utils)
  target_link_libraries(vix_cli PRIVATE Vix::utils)
else()
  find_package(vix_utils QUIET CONFIG)
  if (vix_utils_FOUND)
    target_link_libraries(vix_cli PRIVATE Vix::utils)
  else()
    message(FATAL_ERROR
      "Vix::utils introuvable. Construisez ce module depuis le super-projet vix/ "
      "ou installez vix_utils avec un config package exportant la cible Vix::utils.")
  endif()
endif()

# --- Sortie: binaire 'vix' à la racine du build
set_target_properties(vix_cli PROPERTIES
  OUTPUT_NAME vix
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# --- (Optionnel) Installation
include(GNUInstallDirs)
install(TARGETS vix_cli
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# --- Messages
message(STATUS "CLI module configured.")
message(STATUS "Executable will be: ${CMAKE_BINARY_DIR}/vix")
