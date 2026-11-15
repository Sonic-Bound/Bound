cmake_minimum_required(VERSION 3.25)

# Milestone 9: the desktop wallet, bound-qt, needs Qt 6.
# - On Linux it's the system's package (qt6-base-dev on Debian or Ubuntu), so it's on by default.
#   Without Qt, everything else still builds.
# - On Windows vcpkg has to build Qt (the "gui" feature in vcpkg.json): one to two hours and several
#   GB of memory the first time. So it's off unless asked for: cmake --preset msvc -DBOUND_GUI=ON.
if(CMAKE_HOST_WIN32)
  set(bound_gui_default OFF)
else()
  set(bound_gui_default ON)
endif()
option(BOUND_GUI "Build the desktop wallet (bound-qt), with Qt 6" ${bound_gui_default})
if(BOUND_GUI)
  list(APPEND VCPKG_MANIFEST_FEATURES "gui")
endif()

project(bound VERSION 0.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# On Windows these come from vcpkg (vcpkg.json). On Linux, from the system's packages (on Debian or
# Ubuntu: libsodium-dev libleveldb-dev nlohmann-json3-dev libgtest-dev), whose libsodium has a
# pkg-config file rather than a CMake package.
find_package(unofficial-sodium CONFIG QUIET)
if(NOT unofficial-sodium_FOUND)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
  add_library(unofficial-sodium::sodium ALIAS PkgConfig::SODIUM)
endif()
find_package(GTest CONFIG REQUIRED)
find_package(leveldb CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
  # Keep <windows.h> (pulled in later by the networking code) from defining min/max macros.
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Everything except main() lives in bound_core, so the tests link the same code boundd runs.
add_library(bound_core STATIC
  src/util/hex.cpp
  src/util/serialize.cpp
  src/crypto/sha256.cpp
  src/core/uint256.cpp
  src/core/pow.cpp
  src/core/block.cpp
  src/core/miner.cpp
  src/core/params.cpp
  src/core/chain.cpp
  # milestone 2
  src/util/base58.cpp
  src/crypto/ed25519.cpp
  src/core/address.cpp
  src/core/transaction.cpp
  src/core/merkle.cpp
  src/core/consensus.cpp
  # milestone 3
  src/core/coins.cpp
  src/core/validation.cpp
  src/core/wallet.cpp
  # milestone 4
  src/core/arith_uint256.cpp
  # milestone 5
  src/core/storage.cpp
  src/core/leveldb_store.cpp
  src/core/chaindb.cpp
  # milestone 6
  src/net/socket.cpp
  src/net/protocol.cpp
  src/net/addrman.cpp
  src/net/peerman.cpp
  src/net/connman.cpp
  # milestone 7
  src/core/amount.cpp
  src/core/mempool.cpp
  src/util/base64.cpp
  src/wallet/crypter.cpp
  src/wallet/wallet.cpp
  src/rpc/http.cpp
  src/rpc/server.cpp
  src/rpc/methods.cpp
  # hardening
  src/net/banman.cpp
  src/net/headerssync.cpp
  # milestone 8
  src/index/chainindex.cpp
  src/util/time.cpp
  src/rpc/client.cpp
  src/explorer/explorer.cpp
  src/net/seeds.cpp
  src/seeder/dns.cpp
  src/seeder/dns_server.cpp
  src/seeder/crawler.cpp
  # milestone 9
  src/node/node.cpp
)
target_include_directories(bound_core PUBLIC src)
target_link_libraries(bound_core PUBLIC unofficial-sodium::sodium nlohmann_json::nlohmann_json Threads::Threads PRIVATE leveldb::leveldb)
if(WIN32)
  target_link_libraries(bound_core PUBLIC ws2_32)
endif()

add_executable(boundd src/app/main.cpp)
target_link_libraries(boundd PRIVATE bound_core)

add_executable(bound-cli src/app/cli.cpp)
target_link_libraries(bound-cli PRIVATE bound_core)

add_executable(bound-explorer src/app/explorer_main.cpp)
target_link_libraries(bound-explorer PRIVATE bound_core)

add_executable(bound-seeder src/app/seeder_main.cpp)
target_link_libraries(bound-seeder PRIVATE bound_core)

enable_testing()
add_executable(bound_tests
  tests/test_hex.cpp
  tests/test_serialize.cpp
  tests/test_sha256.cpp
  tests/test_uint256.cpp
  tests/test_pow.cpp
  tests/test_block.cpp
  tests/test_miner.cpp
  tests/test_params.cpp
  tests/test_chain.cpp
  # milestone 2
  tests/test_base58.cpp
  tests/test_ed25519.cpp
  tests/test_address.cpp
  tests/test_transaction.cpp
  tests/test_merkle.cpp
  tests/test_block_body.cpp
  tests/test_consensus.cpp
  # milestone 3
  tests/test_coins.cpp
  tests/test_validation.cpp
  tests/test_chainstate.cpp
  tests/test_wallet.cpp
  # milestone 4
  tests/test_arith_uint256.cpp
  tests/test_reorg.cpp
  # milestone 5
  tests/test_storage.cpp
  tests/test_persistence.cpp
  # milestone 6
  tests/test_protocol.cpp
  tests/test_addrman.cpp
  tests/test_p2p.cpp
  tests/test_net.cpp
  # milestone 7
  tests/test_mempool.cpp
  tests/test_wallet_hd.cpp
  tests/test_rpc.cpp
  # hardening
  tests/test_banman.cpp
  tests/test_headerssync.cpp
  # milestone 8
  tests/test_index.cpp
  tests/test_explorer.cpp
  tests/test_seeder.cpp
)
target_link_libraries(bound_tests PRIVATE bound_core GTest::gtest GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(bound_tests DISCOVERY_MODE PRE_TEST)

# Milestone 9: bound-qt, the desktop wallet. Everything but main() is in bound_gui, for its tests.
if(BOUND_GUI)
  find_package(Qt6 6.4 COMPONENTS Widgets)
  if(NOT Qt6_FOUND)
    message(WARNING "Qt 6 not found, so bound-qt won't be built. On Debian or Ubuntu: sudo apt install "
                    "qt6-base-dev. Or configure with -DBOUND_GUI=OFF to stop this warning.")
  endif()
endif()
if(BOUND_GUI AND Qt6_FOUND)
  add_library(bound_gui STATIC
    src/qt/rpc.cpp
    src/qt/nodehost.cpp
    src/qt/style.cpp
    src/qt/format.cpp
    src/qt/session.cpp
    src/qt/dialogs.cpp
    src/qt/overview.cpp
    src/qt/send.cpp
    src/qt/receive.cpp
    src/qt/transactions.cpp
    src/qt/mining.cpp
    src/qt/network.cpp
    src/qt/console.cpp
    src/qt/mainwindow.cpp
  )
  set_target_properties(bound_gui PROPERTIES AUTOMOC ON)
  target_link_libraries(bound_gui PUBLIC bound_core Qt6::Widgets)
  target_compile_definitions(bound_gui PRIVATE BOUND_VERSION="${PROJECT_VERSION}")

  add_executable(bound-qt WIN32 src/qt/main.cpp)
  target_link_libraries(bound-qt PRIVATE bound_gui)

  add_executable(bound_qt_tests tests/test_qt.cpp)
  target_link_libraries(bound_qt_tests PRIVATE bound_gui GTest::gtest)
  gtest_discover_tests(bound_qt_tests DISCOVERY_MODE PRE_TEST PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

  # On Windows Qt looks for its platform plugin in platforms\ beside the program (vcpkg copies the
  # DLLs, not the plugins). The tests draw off screen.
  if(WIN32)
    set(bound_qt_plugins Qt6::QWindowsIntegrationPlugin)
    if(TARGET Qt6::QOffscreenIntegrationPlugin)
      list(APPEND bound_qt_plugins Qt6::QOffscreenIntegrationPlugin)
    endif()
    foreach(target bound-qt bound_qt_tests)
      foreach(plugin ${bound_qt_plugins})
        add_custom_command(TARGET ${target} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/platforms"
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${plugin}>" "$<TARGET_FILE_DIR:${target}>/platforms/")
      endforeach()
    endforeach()
  endif()
endif()
