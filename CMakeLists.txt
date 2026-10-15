cmake_minimum_required(VERSION 3.26)
project(fintech_core_reconciliation VERSION 0.1.4 LANGUAGES CXX)

enable_testing()
include(CMakePackageConfigHelpers)

if(NOT DEFINED FINTECH_STANDALONE_PACKAGE_PREFIX OR
   NOT IS_ABSOLUTE "${FINTECH_STANDALONE_PACKAGE_PREFIX}")
  message(FATAL_ERROR "core-reconciliation: absolute installed-package prefix is required")
endif()

find_package(fintech_contracts 0.1.4 EXACT CONFIG REQUIRED
  PATHS "${FINTECH_STANDALONE_PACKAGE_PREFIX}" NO_DEFAULT_PATH)
if(NOT fintech_contracts_PACKAGE_ID STREQUAL "trading-contracts" OR
   NOT fintech_contracts_VERSION STREQUAL "0.1.3" OR
   NOT fintech_contracts_CONTRACT_ID STREQUAL
       "trading-contracts.public-package" OR
   NOT fintech_contracts_CONTRACT_REVISION STREQUAL "1" OR
   NOT fintech_contracts_FMI_PARTNER_OPERATIONS_CONTRACT_ID STREQUAL
       "fintech.contracts.fmi-partner-operations" OR
   NOT fintech_contracts_FMI_PARTNER_OPERATIONS_CONTRACT_REVISION STREQUAL
       "1" OR
   NOT TARGET fintech::contracts)
  message(FATAL_ERROR "core-reconciliation: incompatible trading-contracts package")
endif()
find_package(fintech_platform_sdk 0.1.0 CONFIG REQUIRED
  PATHS "${FINTECH_STANDALONE_PACKAGE_PREFIX}" NO_DEFAULT_PATH)
if(NOT fintech_platform_sdk_PACKAGE_ID STREQUAL "trading-platform-sdk" OR
   NOT TARGET fintech::core)
  message(FATAL_ERROR "core-reconciliation: incompatible trading-platform-sdk package")
endif()

add_library(fintech_core_reconciliation INTERFACE)
add_library(fintech::core_reconciliation ALIAS fintech_core_reconciliation)
set_target_properties(fintech_core_reconciliation PROPERTIES
  EXPORT_NAME core_reconciliation)
target_compile_features(fintech_core_reconciliation INTERFACE cxx_std_23)
target_include_directories(fintech_core_reconciliation INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>")
target_link_libraries(fintech_core_reconciliation INTERFACE
  fintech::contracts fintech::core)

add_executable(reconciliation_contract_tests tests/reconciliation_contract_tests.cpp)
target_link_libraries(reconciliation_contract_tests PRIVATE fintech_core_reconciliation)
add_test(NAME reconciliation_contract COMMAND reconciliation_contract_tests)

add_executable(reconciliation_owner_tests tests/reconciliation_owner_tests.cpp)
target_link_libraries(reconciliation_owner_tests PRIVATE fintech_core_reconciliation)
add_test(NAME reconciliation_owner COMMAND reconciliation_owner_tests)

add_executable(reconciliation_recovery_tests tests/reconciliation_recovery_tests.cpp)
target_link_libraries(reconciliation_recovery_tests PRIVATE fintech_core_reconciliation)
add_test(NAME reconciliation_recovery COMMAND reconciliation_recovery_tests)

add_executable(reconciliation_causal_support_tests
  tests/reconciliation_causal_support_tests.cpp)
target_link_libraries(reconciliation_causal_support_tests
  PRIVATE fintech_core_reconciliation)
add_test(NAME reconciliation_causal_support
  COMMAND reconciliation_causal_support_tests)

add_executable(fmi_reconciliation_tests
  tests/fmi_reconciliation_tests.cpp)
target_link_libraries(fmi_reconciliation_tests
  PRIVATE fintech_core_reconciliation)
add_test(NAME fmi_reconciliation COMMAND fmi_reconciliation_tests)

add_executable(fmi_partner_operations_tests
  tests/fmi_partner_operations_tests.cpp)
target_link_libraries(fmi_partner_operations_tests
  PRIVATE fintech_core_reconciliation)
set_target_properties(fmi_partner_operations_tests PROPERTIES
  OUTPUT_NAME core_reconciliation_fmi_partner_operations_tests)
add_test(NAME fmi_partner_operations COMMAND fmi_partner_operations_tests)

install(TARGETS fintech_core_reconciliation EXPORT fintech_core_reconciliationTargets)
install(DIRECTORY include/ DESTINATION include)
install(FILES
  include/fintech/reconciliation/contracts.hpp
  include/fintech/reconciliation/causal_support.hpp
  include/fintech/reconciliation/fmi_reconciliation.hpp
  include/fintech/reconciliation/fmi_reconciliation_owner.hpp
  include/fintech/reconciliation/fmi_partner_operations.hpp
  include/fintech/reconciliation/fmi_partner_operations_owner.hpp
  include/fintech/reconciliation/owner.hpp
  DESTINATION share/fintech/integration-composition/include/fintech/reconciliation)
install(FILES
  tests/reconciliation_contract_tests.cpp
  tests/reconciliation_causal_support_tests.cpp
  tests/fmi_reconciliation_tests.cpp
  tests/fmi_partner_operations_tests.cpp
  tests/reconciliation_owner_tests.cpp
  tests/reconciliation_recovery_tests.cpp
  DESTINATION share/fintech/integration-composition/tests/core-reconciliation)
install(TARGETS
  reconciliation_contract_tests
  reconciliation_causal_support_tests
  fmi_reconciliation_tests
  fmi_partner_operations_tests
  reconciliation_owner_tests
  reconciliation_recovery_tests
  RUNTIME DESTINATION bin)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/ci/package-capability-manifest.json"
  DESTINATION share/fintech/manifests RENAME core-reconciliation.json)
install(EXPORT fintech_core_reconciliationTargets
  FILE fintech_core_reconciliationTargets.cmake
  NAMESPACE fintech::
  DESTINATION lib/cmake/fintech_core_reconciliation)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/fintech_core_reconciliationConfig.cmake" [=[
include(CMakeFindDependencyMacro)
get_filename_component(_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
find_dependency(fintech_contracts 0.1.4 EXACT CONFIG REQUIRED
  PATHS "${_prefix}" NO_DEFAULT_PATH)
if(NOT fintech_contracts_PACKAGE_ID STREQUAL "trading-contracts" OR
   NOT fintech_contracts_VERSION STREQUAL "0.1.3" OR
   NOT fintech_contracts_CONTRACT_ID STREQUAL "trading-contracts.public-package" OR
   NOT fintech_contracts_CONTRACT_REVISION STREQUAL "1" OR
   NOT fintech_contracts_FMI_PARTNER_OPERATIONS_CONTRACT_ID STREQUAL
       "fintech.contracts.fmi-partner-operations" OR
   NOT fintech_contracts_FMI_PARTNER_OPERATIONS_CONTRACT_REVISION STREQUAL "1")
  set(fintech_core_reconciliation_FOUND FALSE)
  set(fintech_core_reconciliation_NOT_FOUND_MESSAGE
    "incompatible trading-contracts package identity")
  return()
endif()
find_dependency(fintech_platform_sdk 0.1.0 CONFIG REQUIRED
  PATHS "${_prefix}" NO_DEFAULT_PATH)
set(fintech_core_reconciliation_PACKAGE_ID "core-reconciliation")
set(fintech_core_reconciliation_VERSION "0.1.4")
set(fintech_core_reconciliation_CONTRACT_ID "core-reconciliation.public-package")
set(fintech_core_reconciliation_CONTRACT_REVISION "5")
set(fintech_core_reconciliation_FMI_CONTRACT_ID
  "core-reconciliation.fmi-reconciliation")
set(fintech_core_reconciliation_FMI_CONTRACT_REVISION "1")
set(fintech_core_reconciliation_FMI_PARTNER_OPERATIONS_CONTRACT_ID
  "core-reconciliation.fmi-partner-operations")
set(fintech_core_reconciliation_FMI_PARTNER_OPERATIONS_CONTRACT_REVISION "1")
set(fintech_core_reconciliation_REQUIRED_FMI_AUTHORIZATION_CONTRACT_ID
  "fintech.contracts.fmi-authorization")
set(fintech_core_reconciliation_REQUIRED_FMI_AUTHORIZATION_CONTRACT_REVISION "2")
set(fintech_core_reconciliation_PACKAGE_MANIFEST
  "${_prefix}/share/fintech/manifests/core-reconciliation.json")
if(NOT EXISTS "${fintech_core_reconciliation_PACKAGE_MANIFEST}")
  message(FATAL_ERROR "core-reconciliation: installed package manifest is missing")
endif()
file(READ "${fintech_core_reconciliation_PACKAGE_MANIFEST}" _manifest)
foreach(_required
    "fintech-package-capability-manifest-v1"
    "\"id\": \"core-reconciliation\""
    "\"version\": \"0.1.4\""
    "\"id\": \"core-reconciliation.public-package\""
    "\"revision\": 5"
    "\"id\": \"core-reconciliation.fmi-partner-operations\"")
  string(FIND "${_manifest}" "${_required}" _found)
  if(_found EQUAL -1)
    message(FATAL_ERROR
      "core-reconciliation: installed package manifest is incompatible: ${_required}")
  endif()
endforeach()
string(JSON _logical_count LENGTH "${_manifest}" logical_contracts)
set(_has_partner_operations FALSE)
if(_logical_count GREATER 0)
  math(EXPR _logical_last "${_logical_count} - 1")
  foreach(_logical_index RANGE ${_logical_last})
    string(JSON _logical_id GET "${_manifest}" logical_contracts
      ${_logical_index} id)
    string(JSON _logical_revision GET "${_manifest}" logical_contracts
      ${_logical_index} revision)
    if(_logical_id STREQUAL
         "core-reconciliation.fmi-partner-operations" AND
       _logical_revision STREQUAL "1")
      set(_has_partner_operations TRUE)
    endif()
  endforeach()
endif()
if(NOT _has_partner_operations)
  message(FATAL_ERROR
    "core-reconciliation: missing partner-operations logical revision 1")
endif()
foreach(_required_path
    "${_prefix}/include/fintech/reconciliation/contracts.hpp"
    "${_prefix}/include/fintech/reconciliation/causal_support.hpp"
    "${_prefix}/include/fintech/reconciliation/fmi_reconciliation.hpp"
    "${_prefix}/include/fintech/reconciliation/fmi_reconciliation_owner.hpp"
    "${_prefix}/include/fintech/reconciliation/fmi_partner_operations.hpp"
    "${_prefix}/include/fintech/reconciliation/fmi_partner_operations_owner.hpp"
    "${_prefix}/include/fintech/reconciliation/owner.hpp"
    "${_prefix}/bin/reconciliation_causal_support_tests"
    "${_prefix}/bin/fmi_reconciliation_tests"
    "${_prefix}/bin/core_reconciliation_fmi_partner_operations_tests")
  if(NOT EXISTS "${_required_path}")
    message(FATAL_ERROR
      "core-reconciliation: required installed artifact is missing: ${_required_path}")
  endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/fintech_core_reconciliationTargets.cmake")
]=])
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/fintech_core_reconciliationConfigVersion.cmake"
  VERSION 0.1.4 COMPATIBILITY SameMinorVersion)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/fintech_core_reconciliationConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/fintech_core_reconciliationConfigVersion.cmake"
  DESTINATION lib/cmake/fintech_core_reconciliation)
